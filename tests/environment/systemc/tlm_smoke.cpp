// Environment smoke test, not a reusable RAM asset or protocol-compliance suite.
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <array>
#include <algorithm>
#include <iostream>

struct Memory : sc_core::sc_module {
    tlm_utils::simple_target_socket<Memory> socket{"socket"};
    std::array<unsigned char, 64> bytes{};
    explicit Memory(sc_core::sc_module_name name) : sc_module(name) {
        socket.register_b_transport(this, &Memory::transport);
    }
    void transport(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
        tx.set_dmi_allowed(false);
        const auto addr = tx.get_address();
        const auto size = tx.get_data_length();
        if (addr >= bytes.size() || size > bytes.size() - addr) {
            tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        if (!tx.is_read() && !tx.is_write()) {
            tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        if (!tx.get_data_ptr() || tx.get_byte_enable_ptr() || tx.get_streaming_width() < size) {
            tx.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }
        sc_core::wait(delay + sc_core::sc_time(5, sc_core::SC_NS));
        delay = sc_core::SC_ZERO_TIME;
        if (tx.is_write()) std::copy_n(tx.get_data_ptr(), size, bytes.data() + addr);
        else std::copy_n(bytes.data() + addr, size, tx.get_data_ptr());
        tx.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

struct Host : sc_core::sc_module {
    tlm_utils::simple_initiator_socket<Host> socket{"socket"};
    bool passed = false;
    SC_HAS_PROCESS(Host);
    explicit Host(sc_core::sc_module_name name) : sc_module(name) { SC_THREAD(run); }
    void run() {
        std::array<unsigned char, 4> data{{12, 34, 56, 78}};
        tlm::tlm_generic_payload tx;
        tx.set_address(4);
        tx.set_data_ptr(data.data());
        tx.set_data_length(data.size());
        tx.set_streaming_width(data.size());
        auto delay = sc_core::SC_ZERO_TIME;
        tx.set_command(tlm::TLM_WRITE_COMMAND);
        socket->b_transport(tx, delay);
        const bool write_ok = tx.is_response_ok();
        data.fill(0);
        tx.set_command(tlm::TLM_READ_COMMAND);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        socket->b_transport(tx, delay);
        const bool read_ok = tx.is_response_ok() && data == std::array<unsigned char, 4>{{12, 34, 56, 78}};
        tx.set_address(63);
        tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        socket->b_transport(tx, delay);
        passed = write_ok && read_ok && tx.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE
            && sc_core::sc_time_stamp() == sc_core::sc_time(10, sc_core::SC_NS)
            && delay == sc_core::SC_ZERO_TIME;
        sc_core::sc_stop();
    }
};

int sc_main(int, char**) {
    Memory memory("memory");
    Host host("host");
    host.socket.bind(memory.socket);
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
    std::cout << "TLM read/write/address-error/time: " << (host.passed ? "PASS" : "FAIL") << '\n';
    return host.passed ? 0 : 1;
}
