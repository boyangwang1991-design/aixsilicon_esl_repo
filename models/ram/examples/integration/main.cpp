#include <ram/model.hpp>
#include <tlm_utils/simple_initiator_socket.h>
#include <iostream>

struct Host : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Host> port{"port"};
    bool passed = false;
    SC_HAS_PROCESS(Host);
    explicit Host(sc_core::sc_module_name name) : sc_module(name) { SC_THREAD(run); }
    void run() {
        unsigned char value = 42;
        tlm::tlm_generic_payload tx;
        tx.set_address(0); tx.set_data_length(1); tx.set_streaming_width(1); tx.set_data_ptr(&value);
        auto delay = sc_core::SC_ZERO_TIME;
        tx.set_command(tlm::TLM_WRITE_COMMAND); port->b_transport(tx, delay);
        const bool written = tx.is_response_ok();
        value = 0;
        tx.set_command(tlm::TLM_READ_COMMAND); port->b_transport(tx, delay);
        passed = written && tx.is_response_ok() && value == 42
            && sc_core::sc_time_stamp() == sc_core::sc_time(4, sc_core::SC_NS);
    }
};
int sc_main(int, char**) {
    aix::esl::ram::Model memory("memory");
    Host host("host"); host.port.bind(memory.memory);
    sc_core::sc_start(20, sc_core::SC_NS);
    memory.request_drain();
    std::cout << "external consumer: " << (host.passed && memory.idle() ? "PASS" : "FAIL") << '\n';
    return host.passed && memory.idle() ? 0 : 1;
}
