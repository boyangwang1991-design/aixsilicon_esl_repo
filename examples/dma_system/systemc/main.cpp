#include <dma/model.hpp>
#include <ram/model.hpp>
#include <rom/model.hpp>
#include <host_master/model.hpp>
#include <tlm_bus/model.hpp>
#include <tlm_utils/simple_target_socket.h>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace sc_core;
using namespace aix::esl;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr std::uint64_t BASE=0x1000,DEST=0x1080,ROM=0x2000;
dma::Config dma_config(unsigned capacity=2,unsigned burst=8){dma::Config c;c.capacity=capacity;c.burst_bytes=burst;c.max_transfer_bytes=256;return c;}
ram::Config ram_config(){ram::Config c;c.capacity_bytes=256;return c;}
rom::Config rom_config(){rom::Config c;c.capacity_bytes=64;return c;}
tlm_bus::Config bus_config(const std::string& mode){tlm_bus::Config c;c.regions={{BASE,256,0},{ROM,64,1}};c.max_outstanding=mode=="downstream_full"?1:4;return c;}
struct Bench:sc_module {
    dma::Model dma0{"dma0",dma_config()},dma1{"dma1",dma_config(1,4)};
    host_master::Model host{"host"},competitor{"competitor"};
    tlm_bus::Model bus;ram::Model ram0{"ram0",ram_config()};rom::Model rom0{"rom0",rom_config()};
    sc_event start,worker_finished;std::string mode;bool done=false,worker_done=false;
    std::vector<unsigned char> pattern;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name n,std::string m):sc_module(n),bus("bus",bus_config(m)),mode(m),pattern(32){
        dma0.memory(bus.targets);dma1.memory(bus.targets);host.memory(bus.targets);competitor.memory(bus.targets);
        bus.initiators(ram0.memory);bus.initiators(rom0.memory);
        for(unsigned i=0;i<pattern.size();++i)pattern[i]=(i*7+3)&255;
        SC_THREAD(run);SC_THREAD(worker);
    }
    void accept(dma::Model& model,std::uint64_t id,std::uint64_t src=BASE,std::uint64_t dst=DEST,unsigned bytes=24){
        check(model.submit({id,src,dst,bytes})==dma::Submit::accepted,"command rejected");
    }
    dma::Completion completion(dma::Model& model){dma::Completion c;while(!model.pop_completion(c))wait(model.completion_event());return c;}
    void expect_data(std::uint64_t address,unsigned bytes,unsigned source_offset=0){std::vector<unsigned char>d(bytes);check(host.read(address,d)==tlm::TLM_OK_RESPONSE,"host read");for(unsigned i=0;i<bytes;++i)check(d[i]==pattern[source_offset+i],"copy mismatch");}
    void expect_zero(std::uint64_t address,unsigned bytes){std::vector<unsigned char>d(bytes);check(host.read(address,d)==tlm::TLM_OK_RESPONSE,"host zero read");for(auto b:d)check(b==0,"unexpected partial write");}
    void worker(){
        if(mode!="reset_read"&&mode!="reset_write"&&mode!="contention"&&mode!="downstream_full"){worker_done=true;return;}
        wait(start);
        if(mode=="reset_read"||mode=="reset_write"){
            wait(mode=="reset_read"?1:4,SC_NS);dma0.reset();check(!dma0.idle()&&!dma0.resume(),"reset cannot release active payload");
        }else{
            std::vector<unsigned char>d(8,0xcc);check(competitor.write(BASE+192,d)==tlm::TLM_OK_RESPONSE,"competing host write");
        }
        worker_done=true;worker_finished.notify(SC_ZERO_TIME);
    }
    void run(){
        check(host.write(BASE,pattern)==tlm::TLM_OK_RESPONSE,"host initialization");
        if(mode=="copy"||mode=="tail"){
            const unsigned bytes=mode=="tail"?19:24;auto before=sc_time_stamp();accept(dma0,1,BASE,DEST,bytes);auto c=completion(dma0);
            check(c.id==1&&c.status==dma::Status::completed&&c.bytes_written==bytes&&c.response==tlm::TLM_OK_RESPONSE,"copy completion");
            check(sc_time_stamp()-before==sc_time(18,SC_NS),"copy service time");expect_data(DEST,bytes);expect_zero(DEST+bytes,1);
        }else if(mode=="capacity"){
            accept(dma0,1);accept(dma0,2,BASE,DEST+32);check(dma0.submit({3,BASE,DEST+64,8})==dma::Submit::full,"queue capacity");
            while(!dma0.idle())wait(dma0.idle_event());check(dma0.submit({3,BASE,DEST+64,8})==dma::Submit::full,"completion credits must stay reserved");
            check(dma0.submit({1,BASE,DEST+64,8})==dma::Submit::duplicate,"duplicate unconsumed ID");
            auto first=completion(dma0);check(first.id==1&&first.status==dma::Status::completed,"FIFO first completion");accept(dma0,3,BASE,DEST+64,8);
            auto second=completion(dma0);auto third=completion(dma0);check(second.id==2&&third.id==3&&third.status==dma::Status::completed,"completion order/count");
            expect_data(DEST,24);expect_data(DEST+32,24);expect_data(DEST+64,8);
        }else if(mode=="drain"){
            accept(dma0,1);accept(dma0,2,BASE,DEST+32);dma0.request_drain();check(!dma0.resume()&&dma0.submit({3,BASE,DEST,8})==dma::Submit::closed,"drain admission");
            check(completion(dma0).status==dma::Status::completed&&completion(dma0).status==dma::Status::completed,"drain preserves accepted commands");
            check(dma0.idle()&&dma0.resume(),"drain resume");accept(dma0,1,BASE,DEST,8);check(completion(dma0).status==dma::Status::completed,"reuse ID/restart");
        }else if(mode=="reset_read"||mode=="reset_write"){
            accept(dma0,1);accept(dma0,2,BASE,DEST+32);start.notify(SC_ZERO_TIME);
            auto queued=completion(dma0);auto active=completion(dma0);
            check(queued.id==2&&queued.status==dma::Status::cancelled&&queued.bytes_written==0,"queued cancellation");
            unsigned written=mode=="reset_write"?8:0;
            check(active.id==1&&active.status==dma::Status::cancelled&&active.bytes_written==written,"active cancellation progress");
            if(written)expect_data(DEST,written);expect_zero(DEST+written,24-written);expect_zero(DEST+32,24);
            check(dma0.resume(),"reset recovery");accept(dma0,3);check(completion(dma0).status==dma::Status::completed,"restart after reset");expect_data(DEST,24);
        }else if(mode=="reset_queued"){
            accept(dma0,1);accept(dma0,2,BASE,DEST+32);dma0.reset();dma0.reset();
            for(unsigned id=1;id<=2;++id){auto c=completion(dma0);check(c.id==id&&c.status==dma::Status::cancelled&&c.bytes_written==0,"reset queued exactly once");}
            expect_zero(DEST,56);check(dma0.idle()&&dma0.resume(),"queued reset idle");
        }else if(mode=="read_error"||mode=="write_error"||mode=="partial_error"){
            auto src=mode=="read_error"?0x3000:BASE;auto dst=mode=="write_error"?ROM:(mode=="partial_error"?BASE+248:DEST);
            accept(dma0,1,src,dst,16);auto c=completion(dma0);
            check(c.status==dma::Status::failed&&c.bytes_written==(mode=="partial_error"?8u:0u),"failed command progress");
            check(c.response==(mode=="write_error"?tlm::TLM_COMMAND_ERROR_RESPONSE:tlm::TLM_ADDRESS_ERROR_RESPONSE),"downstream error propagation");
            if(mode=="partial_error")expect_data(BASE+248,8);else expect_zero(DEST,16);
            accept(dma0,2);check(completion(dma0).status==dma::Status::completed,"recovery after error");
        }else if(mode=="invalid"){
            for(auto c:{dma::Command{1,BASE,DEST,0},dma::Command{1,BASE,DEST,257},dma::Command{1,BASE,BASE+4,8},dma::Command{1,BASE+4,BASE,8},dma::Command{1,BASE,BASE,8},dma::Command{1,std::numeric_limits<std::uint64_t>::max()-1,DEST,8}})
                check(dma0.submit(c)==dma::Submit::invalid,"invalid command accepted");
            accept(dma0,1);check(dma0.submit({1,BASE,DEST,8})==dma::Submit::duplicate,"active duplicate");check(completion(dma0).status==dma::Status::completed,"invalid consumed credit");
        }else if(mode=="dual"){
            accept(dma0,1,BASE,DEST,16);accept(dma1,1,BASE+16,DEST+32,16);
            auto a=completion(dma0),b=completion(dma1);check(a.id==1&&b.id==1&&a.status==dma::Status::completed&&b.status==dma::Status::completed,"independent instances/IDs");
            expect_data(DEST,16);expect_data(DEST+32,16,16);
        }else if(mode=="contention"||mode=="downstream_full"){
            start.notify(SC_ZERO_TIME);
            if(mode=="downstream_full")wait(1,SC_NS);
            accept(dma0,1);auto c=completion(dma0);
            if(mode=="contention"){check(c.status==dma::Status::completed,"shared-link copy failed");expect_data(DEST,24);}
            else check(c.status==dma::Status::failed&&c.response==tlm::TLM_GENERIC_ERROR_RESPONSE&&c.bytes_written==0,"downstream rejection must not retry");
            if(!worker_done)wait(worker_finished);
            std::vector<unsigned char>d(8);check(host.read(BASE+192,d)==tlm::TLM_OK_RESPONSE,"shared RAM read");for(auto b:d)check(b==0xcc,"host data lost");
        }else throw std::runtime_error("unknown case");
        dma::Completion spare;check(!dma0.pop_completion(spare)&&!dma1.pop_completion(spare),"duplicate completion");
        dma0.request_drain();dma1.request_drain();host.request_drain();competitor.request_drain();bus.request_drain();ram0.request_drain();rom0.request_drain();
        check(dma0.idle()&&dma1.idle()&&host.idle()&&competitor.idle()&&bus.idle()&&ram0.idle()&&rom0.idle(),"final idle");done=true;
    }
};
// Independent target uses annotated time only, unlike the wait-based RAM/bus.
struct Annotated:sc_module {
    tlm_utils::simple_target_socket<Annotated> memory{"memory"};
    unsigned calls=0;unsigned char stored=0;sc_time read_time,write_time;
    explicit Annotated(sc_module_name n):sc_module(n){memory.register_b_transport(this,&Annotated::access);}
    void access(tlm::tlm_generic_payload& tx,sc_time& delay){
        check(tx.get_data_length()==1&&delay==SC_ZERO_TIME,"annotated payload");++calls;
        if(tx.is_read()){read_time=sc_time_stamp();*tx.get_data_ptr()=0x5a;}else{write_time=sc_time_stamp();stored=*tx.get_data_ptr();}
        delay+=sc_time(3,SC_NS);tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};
struct AnnotationBench:sc_module {
    dma::Model model{"dma",dma_config()};Annotated target{"target"};bool done=false;
    SC_HAS_PROCESS(AnnotationBench);
    explicit AnnotationBench(sc_module_name n):sc_module(n){model.memory(target.memory);SC_THREAD(run);}
    void run(){auto t=sc_time_stamp();check(model.submit({1,0,8,1})==dma::Submit::accepted,"submit");dma::Completion c;while(!model.pop_completion(c))wait(model.completion_event());
        check(c.status==dma::Status::completed&&c.bytes_written==1&&target.calls==2&&target.stored==0x5a,"annotated data");
        check(target.write_time-target.read_time==sc_time(3,SC_NS)&&sc_time_stamp()-t==sc_time(6,SC_NS),"annotation exactly once");done=true;}
};
int sc_main(int argc,char**argv){std::string mode=argc>1?argv[1]:"copy";try{
    if(mode=="invalid_config"){auto c=dma_config();c.capacity=0;try{dma::Model m("invalid",c);}catch(const std::invalid_argument&e){return std::string(e.what()).find("capacity")!=std::string::npos?0:1;}return 1;}
    if(mode=="unbound"){dma::Model m("unbound");try{sc_start(SC_ZERO_TIME);}catch(const sc_report&e){return std::string(e.what()).find("unbound.memory")!=std::string::npos?0:1;}return 1;}
    if(mode=="annotation"){AnnotationBench b("annotation");sc_start(1,SC_US);check(b.done,"annotation watchdog");return 0;}
    Bench b("bench",mode);sc_start(1,SC_US);check(b.done&&b.worker_done,"watchdog/incomplete");return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
