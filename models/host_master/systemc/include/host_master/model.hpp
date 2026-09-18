#pragma once
#include <host_master/config.hpp>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <memory>
#include <vector>
namespace aix::esl::host_master {
class Model final : public sc_core::sc_module {
public:
    tlm_utils::simple_initiator_socket<Model, 32> memory{"memory"};
    explicit Model(sc_core::sc_module_name name, Config config = {});
    ~Model() override;
    tlm::tlm_response_status read(std::uint64_t address, std::vector<unsigned char>& data);
    tlm::tlm_response_status write(std::uint64_t address, const std::vector<unsigned char>& data);
    bool idle() const;
    const sc_core::sc_event& idle_event() const;
    void request_drain();
    bool resume();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    tlm::tlm_response_status transfer(tlm::tlm_command, std::uint64_t, std::vector<unsigned char>&);
};
}
