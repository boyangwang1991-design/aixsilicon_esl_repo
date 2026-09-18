#include <aix/esl/address_mapper.hpp>
#include <aix/esl/arbiter.hpp>
#include <aix/esl/byte_store.hpp>
#include <aix/esl/event_recorder.hpp>
#include <aix/esl/ordered_completion.hpp>
#include <aix/esl/resource_timing.hpp>
#include <aix/esl/traffic_source.hpp>
#include <aix/esl/verification.hpp>
#include <aix/esl/fault_schedule.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
using namespace aix::esl;
using namespace sc_core;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct System:sc_module{
    struct Flight{unsigned port,bank;uint64_t tag;ResourceTiming::Ticket ticket;Transaction transaction;};
    std::vector<std::unique_ptr<ResourceTiming>> banks;
    std::vector<Arbiter> arbiters;
    ByteStore memory{4096};AddressMapper mapper;
    OrderedCompletion rob{8};EventRecorder recorder;
    ConservationChecker conservation{8};MemoryScoreboard scoreboard{4096};
    FaultSchedule faults{{{"bank0",sc_time(4,SC_NS),sc_time(7,SC_NS),true}}};
    bool inject_fault;
    std::vector<TrafficSource> sources;
    std::vector<std::optional<Transaction>> waiting;
    std::vector<Flight> flights;
    unsigned issued[2]={},completed[2]={};bool passed=false;uint64_t finish_tick=0;
    SC_HAS_PROCESS(System);
    System(sc_module_name name,bool xored,bool trace,unsigned latency,unsigned ii,bool fault=false):sc_module(name),
        mapper(4096,4,16,1,xored?AddressMapper::Policy::xor_interleaved:AddressMapper::Policy::interleaved),
        recorder(trace?EventRecorder::Mode::trace:EventRecorder::Mode::off),inject_fault(fault),waiting(2){
        for(unsigned bank=0;bank<4;++bank){banks.emplace_back(new ResourceTiming(sc_time(latency,SC_NS),sc_time(ii,SC_NS),1,8));arbiters.emplace_back(2);}
        for(unsigned port=0;port<2;++port)sources.emplace_back(port*2048,2048,16,TrafficSource::Pattern::stride,64,100,17,"port"+std::to_string(port));
        SC_THREAD(run);
    }
    void run(){
        for(unsigned cycle=0;cycle<1000;++cycle){
            for(auto it=flights.begin();it!=flights.end();){if(it->ticket.ready>sc_time_stamp()){++it;continue;}
                auto& t=it->transaction;check(memory.write(t.address,t.data.data(),t.data.size()),"store completion");
                scoreboard.write_committed(t);conservation.complete(it->tag,t.data.size());
                banks[it->bank]->retire(it->ticket.id);rob.complete(it->tag,true);
                recorder.emit(it->tag,0,"port"+std::to_string(it->port),"complete","bank"+std::to_string(it->bank),t.data.size());
                it=flights.erase(it);}
            for(unsigned p=0;p<2;++p)while(auto e=rob.retire(p)){++completed[p];recorder.emit(e->tag,0,"port"+std::to_string(p),"retire","response");}
            if(completed[0]+completed[1]==32){
                for(unsigned p=0;p<2;++p)for(unsigned i=0;i<16;++i)for(unsigned byte=0;byte<16;++byte){auto address=p*2048+i*64+byte;
                    check(memory[address]==static_cast<unsigned char>(address),"independent final data oracle");}
                Transaction read;read.data.resize(4096);check(memory.read(0,read.data.data(),read.data.size()),"final read");scoreboard.check_read(read);conservation.finish();
                check(rob.barrier_ready()&&flights.empty(),"transaction conservation");passed=true;finish_tick=sc_time_stamp().value();sc_stop();return;}
            for(unsigned p=0;p<2;++p)if(issued[p]<16&&!waiting[p])waiting[p]=sources[p].next();
            for(unsigned bank=0;bank<4;++bank){std::vector<bool> ready(2,false);
                if(inject_fault&&faults.at("bank"+std::to_string(bank)).pause)continue;
                for(unsigned p=0;p<2;++p)ready[p]=waiting[p]&&mapper.map(waiting[p]->address).bank==bank;
                if(rob.outstanding()==8||(!ready[0]&&!ready[1]))continue;
                auto ticket=banks[bank]->reserve();if(!ticket)continue;
                auto p=*arbiters[bank].grant(ready);auto tag=uint64_t(p)*16+issued[p]++;
                check(rob.submit(tag,p),"reserved ROB credit");auto t=std::move(*waiting[p]);waiting[p].reset();
                conservation.accept(tag,t.data.size());
                recorder.emit(tag,0,"port"+std::to_string(p),"accept","bank"+std::to_string(bank),t.address);
                recorder.emit(tag,0,"port"+std::to_string(p),"service","bank"+std::to_string(bank),t.data.size());
                flights.push_back({p,bank,tag,*ticket,std::move(t)});}
            wait(1,SC_NS);
        }
        throw std::runtime_error("no completion within watchdog limit");
    }
};
int sc_main(int argc,char** argv){try{
    if(argc<5||argc>6)return 2;
    const bool fault=argc==6&&std::string(argv[5])=="fault";
    System system("system",std::string(argv[1])=="xor",std::string(argv[2])=="trace",std::stoul(argv[3]),std::stoul(argv[4]),fault);
    sc_start();check(system.passed,"system did not finish");std::cout<<"completed=32 finish_tick="<<system.finish_tick<<'\n';
    if(argc==6&&!fault){std::ofstream out(argv[5]);system.recorder.write(out);}return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
