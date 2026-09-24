#include <npu_mesh/model.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>
#include <algorithm>
#include <iomanip>
#include <stdexcept>
using namespace aix::esl::npu_mesh;
static std::vector<unsigned char> unhex(const std::string& s){std::vector<unsigned char> d;if(s=="-")return d;if(s.size()%2)throw std::runtime_error("odd hex");for(unsigned i=0;i<s.size();i+=2)d.push_back(std::stoul(s.substr(i,2),nullptr,16));return d;}
static std::string hex(const std::vector<unsigned char>& d){std::ostringstream out;for(auto b:d)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return d.empty()?"-":out.str();}
static Config config(const char* path){Config c;std::ifstream in(path);if(!in)throw std::runtime_error("config open");std::string key;
    std::map<std::string,unsigned*> fields={{"columns",&c.columns},{"rows",&c.rows},{"link_bytes",&c.link_bytes},{"control_bytes",&c.control_bytes},{"depth",&c.depth},{"vcs_per_vn",&c.vcs_per_vn},{"link_latency",&c.link_latency},{"credit_latency",&c.credit_latency},{"router_latency",&c.router_latency},{"packet_bytes",&c.packet_bytes},{"outstanding",&c.outstanding},{"return_bytes",&c.return_bytes},{"command_depth",&c.command_depth},{"command_latency",&c.command_latency},{"timeout_cycles",&c.timeout_cycles},{"max_transfer",&c.max_transfer}};
    fields["fragment_window"]=&c.fragment_window;fields["dma_window"]=&c.dma_window;
    fields["shape_bytes_per_cycle"]=&c.shape_bytes_per_cycle;fields["shape_burst_bytes"]=&c.shape_burst_bytes;fields["shape_context_min"]=&c.shape_context_min;
    while(in>>key){if(key=="endpoint"){Endpoint e;std::string sram_path;
        in>>e.router>>e.base>>e.size>>e.bytes_per_cycle>>e.latency>>e.slots>>e.backend>>e.read_latency>>e.write_latency>>e.turnaround_cycles>>e.priority_context>>e.age_cycles>>e.issue_limit>>std::quoted(sram_path);
        if(e.backend=="sram")e.sram=aix::esl::npu_sram_controller::Config::read(sram_path);
        c.endpoints.push_back(e);}
        else if(key=="split"||key=="centralized"||key=="trace"){unsigned v;in>>v;if(key=="split")c.split=v;else if(key=="trace")c.trace=v;else c.centralized=v;}
        else {auto i=fields.find(key);if(i==fields.end())throw std::runtime_error("unknown configuration field");in>>*i->second;}
        if(!in)throw std::runtime_error("malformed configuration");}c.validate();return c;}
struct Row {unsigned id,release,source,context,axi_id,bytes;std::string op,category;std::uint64_t address;std::vector<unsigned> deps;std::vector<std::uint64_t> destinations;std::vector<unsigned char> data,enables;std::uint64_t handle=0,accepted=0;bool completed=false;};
static std::vector<std::uint64_t> list(const std::string& s){std::vector<std::uint64_t> out;if(s=="-")return out;std::istringstream in(s);std::string item;while(std::getline(in,item,','))out.push_back(std::stoull(item));return out;}
int sc_main(int argc,char** argv){try{
    if(argc!=5)throw std::runtime_error("mesh_run CONFIG TRACE OUTPUT_DIR CYCLE_LIMIT");auto cfg=config(argv[1]);Model m("mesh",cfg);TensorDma dma("tensor_dma",m,cfg.outstanding);
    std::ifstream input(argv[2]);std::string line;std::getline(input,line);if(line!="npu_mesh_trace_v1")throw std::runtime_error("trace version");
    std::vector<Row> rows;while(std::getline(input,line)){if(line.empty())continue;Row r{};std::string deps,dest,data,enables;std::istringstream in(line);
        if(!(in>>r.id>>r.release>>r.source>>r.context>>r.axi_id>>r.op>>r.address>>r.bytes>>deps>>dest>>data>>enables>>r.category))throw std::runtime_error("trace fields");
        for(auto d:list(deps))r.deps.push_back(d);r.destinations=list(dest);r.data=unhex(data);r.enables=unhex(enables);rows.push_back(r);}
    std::string output=argv[3];std::ofstream completions(output+"/completions.tsv");completions<<"id\tstatus\trelease\taccepted\tdone\tbytes\tdata\tcategory\n";
    std::set<unsigned> done;unsigned limit=std::stoul(argv[4]);bool failed=false;
    for(unsigned cycle=0;cycle<limit;++cycle){
        for(auto& r:rows){if(r.completed)continue;
            if(r.handle){
                if(r.op=="COPY"||r.op=="MULTICAST"){DmaCompletion c{};if(!dma.take(r.handle,c))continue;
                    completions<<r.id<<'\t'<<status_name(c.status)<<'\t'<<r.release<<'\t'<<r.accepted<<'\t'<<m.metrics().cycles<<'\t'<<c.bytes<<"\t-\t"<<r.category<<'\n';if(c.status!=Status::ok)failed=true;
                }else{Completion c;if(!m.take(r.handle,c))continue;completions<<r.id<<'\t'<<status_name(c.status)<<'\t'<<r.release<<'\t'<<c.accepted<<'\t'<<c.done<<'\t'<<c.bytes<<'\t'<<hex(c.data)<<'\t'<<r.category<<'\n';if(c.status!=Status::ok)failed=true;}
                r.completed=true;done.insert(r.id);continue;
            }
            if(m.metrics().cycles<r.release||!std::all_of(r.deps.begin(),r.deps.end(),[&](unsigned id){return done.count(id);}))continue;
            Submission s;
            if(r.op=="COPY"||r.op=="MULTICAST"){DmaDescriptor d;d.source=r.source;d.context=r.context;d.id=r.axi_id;for(auto dest:r.destinations)d.segments.push_back({r.address,dest,r.bytes,0});s=dma.submit(d);}
            else {Request q;q.op=r.op=="WRITE"?Op::write:r.op=="READ"?Op::read:Op::fence;q.source=r.source;q.context=r.context;q.id=r.axi_id;q.address=r.address;q.length=r.bytes;q.data=r.data;q.enables=r.enables;s=m.submit(q);}
            if(s.status==Status::ok){r.handle=s.handle;r.accepted=m.metrics().cycles;}
            else if(s.status!=Status::retry)throw std::runtime_error("trace request rejected: "+std::to_string(r.id)+" "+status_name(s.status));
        }
        if(done.size()==rows.size()&&m.idle()&&dma.idle())break;
        sc_core::sc_start(1,sc_core::SC_NS);
    }
    if(done.size()!=rows.size()||!m.idle()||!dma.idle())throw std::runtime_error("cycle watchdog: pending="+std::to_string(rows.size()-done.size()));
    auto s=m.metrics();std::ofstream metrics(output+"/metrics.json");
    metrics<<"{\n\"cycles\":"<<s.cycles<<",\n\"accepted\":"<<s.accepted<<",\n\"completed\":"<<s.completed<<",\n\"useful_bytes\":"<<s.useful_bytes
        <<",\n\"link_bytes\":"<<s.link_bytes<<",\n\"injected_bytes\":"<<s.injected_bytes<<",\n\"header_bytes\":"<<s.header_bytes<<",\n\"mask_bytes\":"<<s.mask_bytes
        <<",\n\"credit_stalls\":"<<s.credit_stalls<<",\n\"allocation_stalls\":"<<s.allocation_stalls<<",\n\"endpoint_stalls\":"<<s.endpoint_stalls
        <<",\n\"admission_stalls\":"<<s.admission_stalls<<",\n\"occupancy_high\":"<<s.occupancy_high<<",\n\"target_bytes\":"<<s.target_bytes<<",\n\"dma_source_bytes\":"<<dma.source_bytes()
        <<",\n\"shape_stalls\":"<<s.shape_stalls<<",\n\"shaped_bytes\":"<<s.shaped_bytes
        <<",\n\"fragment_peak\":"<<s.fragment_peak<<",\n\"return_reserved_peak\":"<<s.return_reserved_peak<<"\n}\n";
    std::ofstream trace(output+"/trace.tsv");trace<<"cycle\thandle\tmodule\tevent\tsource\tcontext\tbytes\n";for(auto& t:m.trace())trace<<t.cycle<<'\t'<<t.handle<<'\t'<<t.module<<'\t'<<t.event<<'\t'<<t.source<<'\t'<<t.context<<'\t'<<t.bytes<<'\n';
    std::ofstream ports(output+"/ports.tsv");ports<<"router\toutput\tvn\tflits\tbytes\tcredit_stalls\tallocation_stalls\tendpoint_stalls\n";
    for(auto& p:m.ports())ports<<p.router<<'\t'<<p.output<<'\t'<<p.vn<<'\t'<<p.flits<<'\t'<<p.bytes<<'\t'<<p.credit_stalls<<'\t'<<p.allocation_stalls<<'\t'<<p.endpoint_stalls<<'\n';
    for(unsigned i=0;i<cfg.endpoints.size();++i){auto& e=cfg.endpoints[i];auto bytes=m.inspect(e.base,e.size);std::ofstream out(output+"/memory_"+std::to_string(i)+".bin",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        std::ofstream target(output+"/target-"+std::to_string(i)+".json");m.target_report(i,target);}
    std::cout<<(failed?"FAIL":"PASS")<<" completed="<<done.size()<<" cycles="<<s.cycles<<"\n";return failed?1:0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
