#include <ram/model.hpp>
#include <tlm_utils/simple_initiator_socket.h>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace sc_core;
using namespace aix::esl;
void check(bool x) { if (!x) throw std::runtime_error("RAM check failed"); }
ram::Config config(const std::string& mode) {
    ram::Config c; c.capacity_bytes=64; c.max_outstanding=2; c.setup_ticks=2; c.bytes_per_tick=4;
    c.enable_counters=mode!="counters_off";
    if(mode=="functional") c.profile=ram::Profile::functional;
    return c;
}
struct Bench: sc_module {
    ram::Model a,b;
    tlm_utils::simple_initiator_socket<Bench> s{"s"}, t{"t"}, u{"u"}, v{"v"};
    std::string mode; bool done=false, first=false, second=false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name n,std::string m):sc_module(n),a("a",config(m)),b("b",config(m)),mode(m) {
        s.bind(a.memory);t.bind(a.memory);u.bind(a.memory);v.bind(b.memory);
        SC_THREAD(run); SC_THREAD(worker); SC_THREAD(control);
    }
    tlm::tlm_response_status tx(tlm_utils::simple_initiator_socket<Bench>& socket,bool write,
        std::uint64_t address,std::vector<unsigned char>& data,unsigned char* be=nullptr,unsigned bel=0,unsigned delay_ns=0) {
        tlm::tlm_generic_payload p; p.set_command(write?tlm::TLM_WRITE_COMMAND:tlm::TLM_READ_COMMAND);
        p.set_address(address);p.set_data_ptr(data.data());p.set_data_length(data.size());p.set_streaming_width(data.size());
        p.set_byte_enable_ptr(be);p.set_byte_enable_length(bel); sc_time d(delay_ns,SC_NS);
        socket->b_transport(p,d);check(d==SC_ZERO_TIME); return p.get_response_status();
    }
    bool concurrent() {return mode=="reset"||mode=="drain"||mode=="capacity";}
    void run() {
        if(concurrent()) {std::vector<unsigned char>d(16,9);auto r=tx(s,true,0,d);check(r==(mode=="reset"?tlm::TLM_GENERIC_ERROR_RESPONSE:tlm::TLM_OK_RESPONSE));first=true;return;}
        if(mode=="invalid") {
            tlm::tlm_generic_payload p; unsigned char bytes[4]={};p.set_data_ptr(bytes);p.set_data_length(4);p.set_streaming_width(4);p.set_read();
            auto reject=[&](tlm::tlm_response_status expected){sc_time d(7,SC_NS);s->b_transport(p,d);check(p.get_response_status()==expected&&d==sc_time(7,SC_NS));};
            p.set_address(std::numeric_limits<std::uint64_t>::max());reject(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            p.set_address(62);reject(tlm::TLM_ADDRESS_ERROR_RESPONSE);p.set_address(0);
            p.set_command(tlm::TLM_IGNORE_COMMAND);reject(tlm::TLM_COMMAND_ERROR_RESPONSE);p.set_read();
            p.set_data_ptr(nullptr);reject(tlm::TLM_GENERIC_ERROR_RESPONSE);p.set_data_ptr(bytes);
            p.set_streaming_width(2);reject(tlm::TLM_BURST_ERROR_RESPONSE);p.set_streaming_width(4);
            unsigned char be=1;p.set_byte_enable_ptr(&be);p.set_byte_enable_length(1);reject(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            be=255;p.set_byte_enable_length(0);reject(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
            tlm::tlm_dmi dmi;check(!s->get_direct_mem_ptr(p,dmi)&&s->transport_dbg(p)==0);done=true;return;
        }
        std::vector<unsigned char>d{1,2,3,4};check(tx(s,true,1,d)==tlm::TLM_OK_RESPONSE);
        d.assign(4,0);check(tx(s,false,1,d)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>({1,2,3,4}));
        unsigned char be[2]={255,0};d.assign(4,9);check(tx(s,true,1,d,be,2)==tlm::TLM_OK_RESPONSE);
        d.assign(4,7);check(tx(s,false,1,d,be,2)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>({9,7,9,7}));
        check(tx(s,false,1,d,nullptr,0,2)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>({9,2,9,4}));
        check(sc_time_stamp()==sc_time(mode=="functional"?2:17,SC_NS));
        check(tx(v,false,1,d)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>(4,0));
        auto c=a.counters();check(c.completed==(mode=="counters_off"?0:5));a.request_drain();check(a.idle()&&a.resume());
        a.reset(true);check(a.resume());auto start=sc_time_stamp();d.assign(4,9);
        check(tx(s,false,1,d)==tlm::TLM_OK_RESPONSE&&d==std::vector<unsigned char>(4,0));
        check(sc_time_stamp()-start==sc_time(mode=="functional"?0:3,SC_NS));done=true;
    }
    void worker() {if(!concurrent())return;wait(1,SC_NS);std::vector<unsigned char>d(4,8);auto r=tx(t,true,32,d);check(r==(mode=="reset"?tlm::TLM_GENERIC_ERROR_RESPONSE:tlm::TLM_OK_RESPONSE));second=true;}
    void control() {
        if(!concurrent())return;wait(2,SC_NS);
        if(mode=="reset") {a.reset(true);check(!a.resume());}
        else {if(mode=="drain")a.request_drain();std::vector<unsigned char>d(1);check(tx(u,false,0,d)==tlm::TLM_GENERIC_ERROR_RESPONSE);}
        if(!a.idle())wait(a.idle_event());check(first&&second);check(sc_time_stamp()==sc_time(mode=="reset"?2:9,SC_NS));check(a.resume());
        std::vector<unsigned char>d(16);check(tx(u,false,0,d)==tlm::TLM_OK_RESPONSE);check(d==std::vector<unsigned char>(16,mode=="reset"?0:9));
        auto c=a.counters();check(c.accepted==3&&c.cancelled==(mode=="reset"?2:0)&&c.completed==(mode=="reset"?1:3));done=true;
    }
};
int sc_main(int argc,char** argv) {
    std::string mode=argc>1?argv[1]:"normal";
    try {
        if(mode=="unbound") {ram::Model m("unbound_ram");try {sc_start(SC_ZERO_TIME);}catch(const sc_report& r){return std::string(r.what()).find("unbound_ram.memory")!=std::string::npos?0:1;}return 1;}
        Bench b("bench",mode);sc_start(100,SC_NS);check(b.done&&b.a.idle()&&b.b.idle());return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
