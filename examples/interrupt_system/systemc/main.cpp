#include <timer/model.hpp>
#include <irq_controller/model.hpp>
#include <host_master/model.hpp>
#include <tlm_bus/model.hpp>
#include <tlm_utils/simple_initiator_socket.h>
#include <iostream>
#include <stdexcept>
using namespace sc_core;
using namespace aix::esl;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr std::uint64_t TIMER=0x20000000, IRQ=0x20001000, TIMER2=0x21000000;
tlm_bus::Config bus_config(){tlm_bus::Config c;c.regions={{TIMER,4096,0},{IRQ,4096,1},{TIMER2,4096,2}};return c;}
struct Bench:sc_module {
    host_master::Model host{"host"};tlm_bus::Model bus{"bus",bus_config()};
    timer::Model timer0{"timer0"},timer1{"timer1"};irq_controller::Model controller{"controller"};
    tlm_utils::simple_initiator_socket<Bench> direct{"direct"},peer{"peer"},direct_irq{"direct_irq"};
    sc_signal<bool> rst{"rst"},irq0{"irq0"},irq1{"irq1"},manual{"manual"},low{"low"},aggregate{"aggregate"};
    sc_event start;
    std::string mode;bool done=false,peer_done=false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name n,std::string m):sc_module(n),mode(m){
        host.memory.bind(bus.targets);bus.initiators.bind(timer0.registers);bus.initiators.bind(controller.registers);bus.initiators.bind(timer1.registers);
        direct.bind(timer0.registers);peer.bind(timer0.registers);direct_irq.bind(controller.registers);
        timer0.reset(rst);timer1.reset(rst);controller.reset(rst);
        timer0.irq(irq0);timer1.irq(irq1);controller.irq(aggregate);
        controller.sources[0](irq0);controller.sources[1](manual);controller.sources[2](irq1);controller.sources[3](low);
        SC_THREAD(run);SC_THREAD(other);SC_THREAD(reset_worker);
    }
    void settle(){for(int i=0;i<8;++i)wait(SC_ZERO_TIME);}
    std::uint32_t read(std::uint64_t address){std::vector<unsigned char>d(4);check(host.read(address,d)==tlm::TLM_OK_RESPONSE,"MMIO read failed");return std::uint32_t(d[0])|(std::uint32_t(d[1])<<8)|(std::uint32_t(d[2])<<16)|(std::uint32_t(d[3])<<24);}
    tlm::tlm_response_status write_status(std::uint64_t address,std::uint32_t value){std::vector<unsigned char>d(4);for(unsigned i=0;i<4;++i)d[i]=(value>>(8*i))&255;return host.write(address,d);}
    void write(std::uint64_t address,std::uint32_t value){check(write_status(address,value)==tlm::TLM_OK_RESPONSE,"MMIO write failed");}
    tlm::tlm_response_status raw(tlm_utils::simple_initiator_socket<Bench>& socket,bool write,std::uint64_t address,std::uint32_t value,unsigned delay_ns=0){
        unsigned char d[4];for(unsigned i=0;i<4;++i)d[i]=(value>>(8*i))&255;
        tlm::tlm_generic_payload tx;tx.set_command(write?tlm::TLM_WRITE_COMMAND:tlm::TLM_READ_COMMAND);tx.set_address(address);tx.set_data_ptr(d);tx.set_data_length(4);tx.set_streaming_width(4);
        sc_time delay(delay_ns,SC_NS);socket->b_transport(tx,delay);if(tx.is_response_ok())check(delay==SC_ZERO_TIME,"delay not consumed");return tx.get_response_status();
    }
    void other(){
        if(mode!="reset"&&mode!="capacity"){peer_done=true;return;}
        wait(start);
        if(mode=="reset")check(raw(direct_irq,true,4,15,10)==tlm::TLM_GENERIC_ERROR_RESPONSE,"IRQ reset must cancel");
        else {wait(1,SC_NS);check(raw(peer,true,4,99)==tlm::TLM_GENERIC_ERROR_RESPONSE,"capacity must reject");}
        peer_done=true;
    }
    void reset_worker(){if(mode!="reset")return;wait(start);wait(1,SC_NS);rst.write(true);wait(2,SC_NS);rst.write(false);}
    void run(){
        settle();
        if(mode=="oneshot"){
            write(IRQ+4,1);write(TIMER+4,10);write(TIMER,5);auto armed=sc_time_stamp();
            wait(9,SC_NS);settle();check(!irq0.read(),"early IRQ");wait(1,SC_NS);settle();
            check(irq0.read()&&aggregate.read()&&sc_time_stamp()-armed==sc_time(10,SC_NS),"expiry/aggregate");
            check(read(TIMER+8)==0&&read(TIMER2+12)==0,"independent timer");
            write(IRQ,1);settle();check(aggregate.read(),"high source must repend");
            write(TIMER+12,1);write(IRQ,1);settle();check(!aggregate.read(),"source clear then controller clear");
        } else if(mode=="periodic"){
            check(raw(direct,true,4,10)==tlm::TLM_OK_RESPONSE,"reload");
            check(raw(direct,true,0,7)==tlm::TLM_OK_RESPONSE,"enable");
            wait(9,SC_NS);check(raw(direct,true,12,1)==tlm::TLM_OK_RESPONSE,"W1C collision");settle();check(irq0.read(),"expiry must win same-time W1C");
            check(raw(direct,true,12,1)==tlm::TLM_OK_RESPONSE,"clear");settle();check(!irq0.read(),"clear after expiry");
            wait(9,SC_NS);settle();check(irq0.read(),"second periodic expiry");
            timer0.request_drain();if(!timer0.idle())wait(timer0.idle_event());check(timer0.resume(),"resume after drain");
            write(TIMER+12,1);wait(20,SC_NS);settle();check(!irq0.read(),"drain cancels future periodic events");
        } else if(mode=="reprogram"){
            write(TIMER+4,20);write(TIMER,5);wait(3,SC_NS);write(TIMER+4,30);auto rearmed=sc_time_stamp();
            wait(20,SC_NS);settle();check(!irq0.read(),"stale expiry after reprogram");
            wait(10,SC_NS);settle();check(irq0.read()&&sc_time_stamp()-rearmed==sc_time(30,SC_NS),"reprogram deadline");
        } else if(mode=="irq"){
            manual.write(true);settle();check(!aggregate.read(),"masked IRQ");check(read(IRQ)==2,"latch while masked");
            write(IRQ+4,3);settle();check(aggregate.read()&&read(IRQ+12)==1,"unmask/priority");
            write(TIMER+4,2);write(TIMER,5);wait(2,SC_NS);settle();check(read(IRQ+12)==0,"lowest index priority");
            manual.write(false);write(TIMER+12,1);write(IRQ,3);settle();check(!aggregate.read()&&read(IRQ+12)==32,"clear all");
        } else if(mode=="reset"||mode=="capacity"){
            if(mode=="reset"){write(IRQ+4,1);write(TIMER+4,1);write(TIMER,5);wait(1,SC_NS);settle();check(aggregate.read(),"IRQ before reset");}
            start.notify(SC_ZERO_TIME);auto response=raw(direct,true,4,99,10);
            check(response==(mode=="reset"?tlm::TLM_GENERIC_ERROR_RESPONSE:tlm::TLM_OK_RESPONSE),"in-flight outcome");
            if(mode=="reset"){
                check(!timer0.resume()&&!controller.resume(),"resume while reset held");wait(rst.negedge_event());settle();
                check(timer0.resume()&&timer1.resume()&&controller.resume(),"resume after reset");
                check(read(TIMER+4)==10&&read(IRQ+4)==0&&!aggregate.read(),"reset values");
                write(TIMER,5);wait(10,SC_NS);settle();check(irq0.read(),"restart after reset");
            }else check(read(TIMER+4)==99,"accepted write preserved");
            check(peer_done,"peer did not complete");
        } else if(mode=="errors"){
            check(write_status(TIMER+8,1)==tlm::TLM_COMMAND_ERROR_RESPONSE,"RO write");
            check(write_status(TIMER+4,0)==tlm::TLM_COMMAND_ERROR_RESPONSE,"zero reload");
            check(write_status(TIMER,8)==tlm::TLM_COMMAND_ERROR_RESPONSE,"reserved control");
            check(write_status(IRQ+4,16)==tlm::TLM_COMMAND_ERROR_RESPONSE,"invalid source mask");
            check(write_status(TIMER+16,1)==tlm::TLM_ADDRESS_ERROR_RESPONSE,"unknown register");
            unsigned char data[4]={},be=0;tlm::tlm_generic_payload tx;tx.set_write();tx.set_address(4);tx.set_data_ptr(data);tx.set_data_length(4);tx.set_streaming_width(4);tx.set_byte_enable_ptr(&be);tx.set_byte_enable_length(1);sc_time d(3,SC_NS);
            direct->b_transport(tx,d);check(tx.get_response_status()==tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE&&d==sc_time(3,SC_NS),"partial enables rejected without time");
            tx.set_byte_enable_ptr(nullptr);tx.set_address(1);direct->b_transport(tx,d);check(tx.get_response_status()==tlm::TLM_ADDRESS_ERROR_RESPONSE,"unaligned");
            tx.set_address(0);tx.set_data_length(2);direct->b_transport(tx,d);check(tx.get_response_status()==tlm::TLM_BURST_ERROR_RESPONSE,"narrow access");
            tlm::tlm_dmi info;check(!direct->get_direct_mem_ptr(tx,info)&&direct->transport_dbg(tx)==0,"DMI/debug disabled");
        } else if(mode=="transport"){
            auto before=sc_time_stamp();check(raw(direct,true,4,42,7)==tlm::TLM_OK_RESPONSE,"delayed access");check(sc_time_stamp()-before==sc_time(8,SC_NS),"double charged delay");
            before=sc_time_stamp();check(read(TIMER+4)==42,"little endian");check(sc_time_stamp()-before==sc_time(2,SC_NS),"bus plus device latency");
            timer0.request_drain();check(raw(direct,true,4,8)==tlm::TLM_GENERIC_ERROR_RESPONSE,"closed admission");check(timer0.resume(),"resume");check(read(TIMER+4)==42,"rejection changed register");
        }
        timer0.request_drain();timer1.request_drain();controller.request_drain();host.request_drain();bus.request_drain();
        check(timer0.idle()&&timer1.idle()&&controller.idle()&&host.idle()&&bus.idle(),"not drained");done=true;
    }
};
int sc_main(int argc,char**argv){std::string mode=argc>1?argv[1]:"oneshot";try{
    if(mode=="invalid_config"){irq_controller::Config c;c.sources=33;try{irq_controller::Model m("bad_irq",c);}catch(const std::invalid_argument&e){return std::string(e.what()).find("sources must be")!=std::string::npos?0:1;}return 1;}
    if(mode=="invalid_timer"){timer::Config c;c.tick=SC_ZERO_TIME;try{timer::Model m("bad_timer",c);}catch(const std::invalid_argument&e){return std::string(e.what()).find("interval overflow")!=std::string::npos?0:1;}return 1;}
    if(mode=="unbound_irq"){irq_controller::Model m("unbound_irq");try{sc_start(SC_ZERO_TIME);}catch(const sc_report&e){return std::string(e.what()).find("unbound_irq")!=std::string::npos?0:1;}return 1;}
    if(mode=="unbound"){timer::Model t("unbound_timer");try{sc_start(SC_ZERO_TIME);}catch(const sc_report&e){return std::string(e.what()).find("unbound_timer")!=std::string::npos?0:1;}return 1;}
    Bench b("bench",mode);sc_start(1,SC_US);check(b.done&&b.peer_done,"watchdog/incomplete workload");return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
