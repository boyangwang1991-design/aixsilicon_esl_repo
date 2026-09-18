#include <host_master/model.hpp>
#include <aix/esl/blocking_gate.hpp>
#include <limits>
namespace aix::esl::host_master {
struct Model::Impl { aix::esl::BlockingGate gate; explicit Impl(Config c) : gate(c.max_outstanding) {} };
Model::Model(sc_core::sc_module_name name, Config c) : sc_module(name), impl_(std::make_unique<Impl>(c)) {}
Model::~Model() = default;
bool Model::idle() const { return impl_->gate.idle(); }
const sc_core::sc_event& Model::idle_event() const { return impl_->gate.idle_event(); }
void Model::request_drain() { impl_->gate.drain(); }
bool Model::resume() { return impl_->gate.resume(); }
tlm::tlm_response_status Model::read(std::uint64_t address, std::vector<unsigned char>& data) {
    return transfer(tlm::TLM_READ_COMMAND, address, data);
}
tlm::tlm_response_status Model::write(std::uint64_t address, const std::vector<unsigned char>& data) {
    auto copy = data;
    return transfer(tlm::TLM_WRITE_COMMAND, address, copy);
}
tlm::tlm_response_status Model::transfer(tlm::tlm_command command, std::uint64_t address,
                                          std::vector<unsigned char>& data) {
    const auto process = sc_core::sc_get_current_process_handle();
    if (!process.valid() || process.proc_kind() == sc_core::SC_METHOD_PROC_ || data.empty() ||
        data.size() > std::numeric_limits<unsigned>::max())
        return tlm::TLM_GENERIC_ERROR_RESPONSE;
    aix::esl::BlockingLease lease(impl_->gate);
    if (!lease) return tlm::TLM_GENERIC_ERROR_RESPONSE;
    tlm::tlm_generic_payload tx;
    tx.set_command(command); tx.set_address(address); tx.set_data_ptr(data.data());
    tx.set_data_length(data.size()); tx.set_streaming_width(data.size());
    tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    auto delay = sc_core::SC_ZERO_TIME;
    memory->b_transport(tx, delay);
    if (delay != sc_core::SC_ZERO_TIME) sc_core::wait(delay);
    return tx.get_response_status();
}
}
