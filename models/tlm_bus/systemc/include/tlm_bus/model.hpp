#pragma once
#include <tlm_bus/config.hpp>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <memory>
namespace aix::esl::tlm_bus {
class Model final : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<Model, 32> targets{"targets"};
    tlm_utils::multi_passthrough_initiator_socket<Model, 32> initiators{"initiators"};
    explicit Model(sc_core::sc_module_name name, Config config);
    ~Model() override;
    bool idle() const;
    const sc_core::sc_event& idle_event() const;
    void request_drain();
    bool resume();
protected:
    void end_of_elaboration() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void transport(int, tlm::tlm_generic_payload&, sc_core::sc_time&);
    bool dmi(int, tlm::tlm_generic_payload&, tlm::tlm_dmi&);
    unsigned debug(int, tlm::tlm_generic_payload&);
};
}
