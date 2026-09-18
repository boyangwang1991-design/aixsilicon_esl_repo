#pragma once
#include <{{name}}/config.hpp>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
namespace aix::esl::{{name}} {
class Model final : public sc_core::sc_module {
public:
    tlm_utils::simple_target_socket<Model,32> registers{"registers"};
    Model(sc_core::sc_module_name, Config = {});
    bool idle() const { return true; }
    void request_drain() { accepting_ = false; }
    bool resume() { accepting_ = true; return true; }
    void reset() { value_ = config_.initial_value; accepting_ = false; }
private:
    Config config_; std::uint32_t value_; bool accepting_ = true;
    void transport(tlm::tlm_generic_payload&, sc_core::sc_time&);
    bool dmi(tlm::tlm_generic_payload&, tlm::tlm_dmi&);
    unsigned debug(tlm::tlm_generic_payload&);
};
}
