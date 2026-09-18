#pragma once
#include <dma/config.hpp>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <memory>
namespace aix::esl::dma {
struct Completion {
    std::uint64_t id;
    Status status;
    unsigned bytes_written;
    tlm::tlm_response_status response;
};
class Model final : public sc_core::sc_module {
public:
    tlm_utils::simple_initiator_socket<Model, 32> memory{"memory"};
    SC_HAS_PROCESS(Model);
    explicit Model(sc_core::sc_module_name name, Config config = {});
    ~Model() override;
    Submit submit(Command command);
    bool pop_completion(Completion& completion);
    const sc_core::sc_event& completion_event() const;
    bool idle() const;
    const sc_core::sc_event& idle_event() const;
    void request_drain();
    void reset(); // Stops future blocks; an issued blocking transaction must finish.
    bool resume();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void run();
};
}
