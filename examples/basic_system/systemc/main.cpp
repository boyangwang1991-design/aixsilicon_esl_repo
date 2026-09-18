#include <host_master/model.hpp>
#include <tlm_bus/model.hpp>
#include <ram/model.hpp>
#include <rom/model.hpp>
#include <stdexcept>
#include <iostream>
using namespace sc_core;
using namespace aix::esl;
void check(bool x){if(!x)throw std::runtime_error("basic_system scoreboard failed");}
ram::Config ram_config(){ram::Config c;c.capacity_bytes=1024*1024;return c;}
rom::Config rom_config(){rom::Config c;c.capacity_bytes=65536;c.initial_data={0x12,0x34,0x56,0x78};c.read_only=false;return c;}
tlm_bus::Config bus_config(const std::string& mode){tlm_bus::Config c;c.regions={{0,65536,0},{0x10000000,1024*1024,1}};if(mode=="capacity")c.max_outstanding=1;if(mode=="invalid_map")c.regions[1].base=4;if(mode=="invalid_binding")c.regions[1].target=2;return c;}
struct System:sc_module {
    host_master::Model host{"host", host_master::Config{1}},peer{"peer"};
    tlm_utils::simple_initiator_socket<System> raw{"raw"};
    tlm_bus::Model bus;
    ram::Model ram{"ram",ram_config()};rom::Model rom{"rom",rom_config()};
    std::string mode;bool done=false,peer_done=false;
    SC_HAS_PROCESS(System);
    System(sc_module_name n,std::string m):sc_module(n),bus("bus",bus_config(m)),mode(m){
        raw.bind(bus.targets);host.memory.bind(bus.targets);peer.memory.bind(bus.targets);bus.initiators.bind(rom.memory);bus.initiators.bind(ram.memory);SC_THREAD(run);SC_THREAD(other);
    }
    void other(){if(mode!="concurrent"&&mode!="capacity"&&mode!="host_capacity"){peer_done=true;return;}wait(1,SC_NS);std::vector<unsigned char>d(4,7);auto response=(mode=="host_capacity"?host:peer).write(0x10000020,d);check(response==((mode=="capacity"||mode=="host_capacity")?tlm::TLM_GENERIC_ERROR_RESPONSE:tlm::TLM_OK_RESPONSE));peer_done=true;}
    void run(){
        std::vector<unsigned char>d{1,2,3,4};check(host.write(0x10000001,d)==tlm::TLM_OK_RESPONSE);
        if(mode!="concurrent")check(sc_time_stamp()==sc_time(3,SC_NS));
        d.assign(4,0);check(host.read(0x10000001,d)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>({1,2,3,4}));
        check(host.read(0,d)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>({0x12,0x34,0x56,0x78}));
        check(host.write(0,d)==tlm::TLM_COMMAND_ERROR_RESPONSE);
        check(host.read(0x30000000,d)==tlm::TLM_ADDRESS_ERROR_RESPONSE);
        check(host.read(65534,d)==tlm::TLM_ADDRESS_ERROR_RESPONSE);
        rom.reset(true);wait(SC_ZERO_TIME);check(rom.resume());check(host.read(0,d)==tlm::TLM_OK_RESPONSE&&d[0]==0x12);
        if(mode=="drain"){
            host.request_drain();check(host.read(0,d)==tlm::TLM_GENERIC_ERROR_RESPONSE);check(host.resume());
            bus.request_drain();check(host.read(0,d)==tlm::TLM_GENERIC_ERROR_RESPONSE);check(bus.resume());
            ram.request_drain();check(host.read(0x10000000,d)==tlm::TLM_GENERIC_ERROR_RESPONSE);check(ram.resume());
            check(host.read(0x10000001,d)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>({1,2,3,4}));
        }
        if(mode=="concurrent"){check(peer_done);check(host.read(0x10000020,d)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>(4,7));}
        if(mode=="transport") {
            tlm::tlm_generic_payload tx;tx.set_read();tx.set_address(0x10000001);tx.set_data_ptr(d.data());tx.set_data_length(4);tx.set_streaming_width(4);
            sc_time delay(7,SC_NS);auto start=sc_time_stamp();raw->b_transport(tx,delay);
            check(tx.is_response_ok()&&tx.get_address()==0x10000001&&delay==SC_ZERO_TIME&&sc_time_stamp()-start==sc_time(10,SC_NS));
            tlm::tlm_dmi info;check(!raw->get_direct_mem_ptr(tx,info)&&raw->transport_dbg(tx)==0);
        }
        host.request_drain();peer.request_drain();bus.request_drain();ram.request_drain();rom.request_drain();
        check(host.idle()&&peer.idle()&&bus.idle()&&ram.idle()&&rom.idle());done=true;
    }
};
int sc_main(int argc,char** argv){
    std::string mode=argc>1?argv[1]:"normal";
    try{System s("system",mode);sc_start(1,SC_US);check(s.done&&s.peer_done);return mode=="invalid_map"||mode=="invalid_binding"?1:0;}
    catch(const std::invalid_argument& e){return mode=="invalid_map"&&std::string(e.what()).find("overlapping")!=std::string::npos?0:1;}
    catch(const sc_report& e){if(mode=="invalid_binding"&&std::string(e.what()).find("region target has no bound initiator")!=std::string::npos)return 0;std::cerr<<e.what()<<'\n';return 1;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
