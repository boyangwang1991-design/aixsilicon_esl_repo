#include "npu_sram_controller/model.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <deque>
#include <algorithm>
using namespace aix::esl::npu_sram_controller;
struct Task{
    unsigned id=0,port=0;char op='R';uint64_t address=0,bytes=0,earliest=0,cycles=0;
    unsigned beat=128,burst=16,axi_id=0,mask_period=1,wdelay=0;
    std::vector<unsigned> deps;bool started=false,done=false;uint64_t release=0,finish=0,sent=0;unsigned inflight=0;
};
struct PendingWrite{uint64_t token,address,ready;unsigned beat,beats,next,mask_period;};
static void step(){sc_core::sc_start(1,sc_core::SC_NS);sc_core::sc_start(sc_core::SC_ZERO_TIME);}
int sc_main(int argc,char**argv){
    if(argc<4){std::cerr<<"usage: npu_sram_run config.kv workload.dag result.json [trace.jsonl]\n";return 2;}
    try{auto cfg=Config::read(argv[1]);Model model("sram",cfg);std::ifstream input(argv[2]);if(!input)throw std::runtime_error("DAG open");
        std::vector<Task> tasks;std::string line;uint64_t expected_r=0,expected_w=0;
        while(std::getline(input,line)){if(line.empty()||line[0]=='#')continue;std::istringstream s(line);Task t;std::string dependencies;
            if(!(s>>t.id>>t.port>>t.op>>t.address>>t.bytes>>t.beat>>t.burst>>t.axi_id>>t.earliest>>t.cycles>>t.mask_period>>t.wdelay>>dependencies))throw std::invalid_argument("DAG row");
            if(t.id!=tasks.size()||t.port>=cfg.ports||t.axi_id>=cfg.ids||!t.mask_period||t.mask_period>128||std::string("RWC").find(t.op)==std::string::npos)throw std::invalid_argument("DAG fields");
            if(dependencies!="-"){std::replace(dependencies.begin(),dependencies.end(),',',' ');std::istringstream d(dependencies);unsigned id;while(d>>id){if(id>=t.id)throw std::invalid_argument("DAG not topological");t.deps.push_back(id);}}
            if(t.op!='C'){if(!t.bytes||t.beat<8||t.beat>128||(t.beat&(t.beat-1))||t.bytes%t.beat||t.address%t.beat||!t.burst||t.burst>32||t.address+ t.bytes>cfg.capacity)throw std::invalid_argument("DAG transfer");
                if(t.op=='R')expected_r+=t.bytes;else expected_w+=t.bytes/t.beat*((t.beat+t.mask_period-1)/t.mask_period);}
            tasks.push_back(t);
        }if(tasks.empty())throw std::invalid_argument("empty DAG");
        std::ofstream trace;if(argc>4){trace.open(argv[4]);model.set_observer([&](const std::string& e){trace<<e<<'\n';});}
        std::unordered_map<uint64_t,unsigned> owners;std::vector<std::deque<PendingWrite>> writes(cfg.ports);
        std::vector<uint64_t> compute_until(cfg.ports);uint64_t done=0,active=0,stall=0,last_progress=0,kernel_finish=0;bool stopped=false;
        while(!stopped||!model.idle()){
            auto now=model.cycle();bool progress=false;
            for(unsigned p=0;p<cfg.ports;++p)for(bool wr:{false,true}){Response r;if(model.pop(p,wr,r)){
                if(r.error)throw std::runtime_error("workload response SLVERR");if(r.last){auto it=owners.find(r.token);if(it==owners.end())throw std::logic_error("orphan response");auto& t=tasks[it->second];--t.inflight;owners.erase(it);progress=true;}}}
            for(auto& t:tasks){if(t.done)continue;if(t.started&&t.op=='C'&&now>=t.finish){t.done=true;++done;progress=true;}
                if(t.started&&t.op!='C'&&t.sent==t.bytes&&!t.inflight){t.done=true;t.finish=now;++done;progress=true;}
                if(t.done&&trace)trace<<"{\"cycle\":"<<now<<",\"event\":\"task_finish\",\"task\":"<<t.id<<",\"port\":"<<t.port<<",\"op\":\""<<t.op<<"\",\"start\":"<<t.release<<"}\n";}
            std::vector<bool> issued_r(cfg.ports),issued_w(cfg.ports),compute_wait(cfg.ports),compute_live(cfg.ports);
            for(auto& t:tasks){if(t.done)continue;bool deps=true;for(auto d:t.deps)deps&=tasks[d].done;
                if(t.op=='C'&&now>=t.earliest){if(!deps)compute_wait[t.port]=true;}
                if(!t.started){if(!deps||now<t.earliest)continue;if(t.op=='C'&&compute_until[t.port]>now)continue;
                    t.started=true;t.release=now;if(t.op=='C'){t.finish=now+t.cycles;compute_until[t.port]=t.finish;}progress=true;
                    if(trace)trace<<"{\"cycle\":"<<now<<",\"event\":\"task_start\",\"task\":"<<t.id<<",\"port\":"<<t.port<<",\"op\":\""<<t.op<<"\"}\n";}
                if(t.op=='C'){compute_live[t.port]=true;continue;}if(t.sent==t.bytes)continue;bool wr=t.op=='W';auto& used=wr?issued_w:issued_r;if(used[t.port])continue;
                uint64_t address=t.address+t.sent;unsigned beats=std::min<uint64_t>({t.burst,(t.bytes-t.sent)/t.beat,(4096-address%4096)/t.beat});
                Request r{t.port,t.axi_id,t.beat,beats,address,t.release,wr};auto token=model.submit(r);if(!token)continue;
                used[t.port]=true;t.sent+=uint64_t(beats)*t.beat;++t.inflight;owners[token]=t.id;
                if(wr)writes[t.port].push_back({token,address,now+t.wdelay,t.beat,beats,0,t.mask_period});progress=true;
            }
            for(unsigned p=0;p<cfg.ports;++p){if(compute_live[p])++active;else if(compute_wait[p])++stall;
                if(writes[p].empty())continue;auto& w=writes[p].front();if(w.ready>now)continue;
                std::vector<uint8_t> data(w.beat),mask(w.beat);for(unsigned i=0;i<w.beat;++i){data[i]=uint8_t((w.address+w.next*w.beat+i)*17+3);mask[i]=i%w.mask_period==0;}
                if(model.push_w(p,data,mask,w.next+1==w.beats)){++w.next;w.ready=now+1;progress=true;if(w.next==w.beats)writes[p].pop_front();}}
            if(done==tasks.size()&&!stopped){kernel_finish=now;model.stop_scrub();stopped=true;}
            if(progress)last_progress=now;
            if(now-last_progress>200000)throw std::runtime_error("no progress: "+model.snapshot());
            if(!stopped||!model.idle())step();
        }
        auto& metrics=model.metrics();if(metrics.read_bytes!=expected_r||metrics.external_write_bytes!=expected_w||metrics.write_bytes!=expected_w)throw std::logic_error("DAG byte conservation");
        std::ostringstream base;model.report(base);auto text=base.str();text.pop_back();std::ofstream result(argv[3]);if(!result)throw std::runtime_error("result open");
        result<<text<<",\"makespan\":"<<kernel_finish<<",\"compute_active_tile_cycles\":"<<active<<",\"dependency_wait_tile_cycles\":"<<stall<<",\"tasks\":"<<tasks.size()<<",\"expected_read_bytes\":"<<expected_r<<",\"expected_write_bytes\":"<<expected_w<<",\"checks\":[\"transaction_conservation\",\"fragment_conservation\",\"finite_capacity\",\"independent_workload_byte_totals\"]}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(argc>3){std::ofstream out(argv[3]);out<<"{\"status\":\"FAIL\",\"error\":\"execution failed; see stderr.log\"}\n";}return 1;}
}
