#pragma once
#include <ram/config.hpp>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <memory>

namespace aix::esl::ram {
class Model : public sc_core::sc_module {
public:
    tlm_utils::multi_passthrough_target_socket<Model, 32> memory{"memory"};
    explicit Model(sc_core::sc_module_name name, Config config = {});
    ~Model() override;
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    const Config& config() const noexcept;
    bool idle() const noexcept;
    const sc_core::sc_event& idle_event() const noexcept;
    void request_drain(); // Close admission; accepted calls finish normally.
    bool resume();        // Reopen admission only when idle.
    void reset(bool clear_memory = true); // Cancel outstanding calls; remain closed.
    Counters counters() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void transport(int, tlm::tlm_generic_payload&, sc_core::sc_time&);
    bool dmi(int, tlm::tlm_generic_payload&, tlm::tlm_dmi&);
    unsigned debug(int, tlm::tlm_generic_payload&);
};
}
