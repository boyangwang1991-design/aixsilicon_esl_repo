#pragma once
#include <uart/config.hpp>
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <memory>
namespace aix::esl::uart {
class Model final : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<Model, 32> registers{"registers"};
    sc_core::sc_in<bool> reset{"reset"};
    sc_core::sc_out<bool> irq{"irq"};
    sc_core::sc_fifo_out<unsigned char> tx{"tx"};
    bool receive(unsigned char byte);
    SC_HAS_PROCESS(Model);
    explicit Model(sc_core::sc_module_name name, Config config = {});
    ~Model() override;
    bool idle() const;
    const sc_core::sc_event& idle_event() const;
    void request_drain();
    bool resume();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void transport(int, tlm::tlm_generic_payload&, sc_core::sc_time&);
    bool dmi(int, tlm::tlm_generic_payload&, tlm::tlm_dmi&);
    unsigned debug(int, tlm::tlm_generic_payload&);
    void handle_reset();
    void update_outputs();
    void run();
};
}
