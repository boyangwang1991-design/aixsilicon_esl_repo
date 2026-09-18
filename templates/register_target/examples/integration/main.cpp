#include <{{name}}/model.hpp>
#include <tlm_utils/simple_initiator_socket.h>
#include <stdexcept>
using namespace sc_core;
using namespace aix::esl;
void check(bool v){if(!v)throw std::runtime_error("register consumer check");}
struct Bench:sc_module {
    {{name}}::Model a{"a"},b{"b",{{name}}::Config{42,sc_time(2,SC_NS)}};
    tlm_utils::simple_initiator_socket<Bench> sa{"sa"},sb{"sb"}; bool done=false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name n):sc_module(n){sa.bind(a.registers);sb.bind(b.registers);SC_THREAD(run);}
    void run(){
        unsigned char data[4]={7,0,0,0};tlm::tlm_generic_payload tx;
        tx.set_write();tx.set_address(0);tx.set_data_ptr(data);tx.set_data_length(4);tx.set_streaming_width(4);
        sc_time delay(3,SC_NS);sa->b_transport(tx,delay);check(tx.is_response_ok()&&delay==sc_time(4,SC_NS)&&sc_time_stamp()==SC_ZERO_TIME);
        wait(delay);tx.set_read();delay=SC_ZERO_TIME;sb->b_transport(tx,delay);check(data[0]==42&&delay==sc_time(2,SC_NS));wait(delay);
        delay=SC_ZERO_TIME;sa->b_transport(tx,delay);check(data[0]==7);wait(delay);
        a.request_drain();delay=SC_ZERO_TIME;sa->b_transport(tx,delay);check(tx.is_response_error()&&delay==SC_ZERO_TIME);a.reset();check(a.resume());
        sa->b_transport(tx,delay);check(tx.is_response_ok()&&data[0]==0);wait(delay);
        tx.set_address(4);delay=SC_ZERO_TIME;sa->b_transport(tx,delay);check(tx.get_response_status()==tlm::TLM_ADDRESS_ERROR_RESPONSE&&delay==SC_ZERO_TIME);
        tlm::tlm_dmi dmi;check(!sa->get_direct_mem_ptr(tx,dmi)&&sa->transport_dbg(tx)==0);done=true;
    }
};
int sc_main(int argc,char**){try{if(argc>1){ {{name}}::Model m("unbound");try{sc_start(SC_ZERO_TIME);}catch(const sc_report& e){return std::string(e.what()).find("unbound.registers")!=std::string::npos?0:1;}return 1;} Bench b("bench");sc_start(100,SC_NS);check(b.done);return 0;}catch(const std::exception&){return 1;}}
