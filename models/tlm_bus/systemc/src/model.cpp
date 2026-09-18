#include <tlm_bus/model.hpp>
#include <aix/esl/blocking_gate.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace aix::esl::tlm_bus {
struct Model::Impl {
    Config config;
    aix::esl::BlockingGate gate;
    sc_core::sc_mutex server;
    explicit Impl(Config c) : config(std::move(c)), gate(config.max_outstanding) {
        if (config.regions.empty()) throw std::invalid_argument("tlm_bus: regions must not be empty");
        auto sorted = config.regions;
        std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) { return a.base < b.base; });
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            auto r = sorted[i];
            if (!r.size || r.base > std::numeric_limits<std::uint64_t>::max() - r.size)
                throw std::invalid_argument("tlm_bus: empty/overflowing region");
            if (i && sorted[i-1].base + sorted[i-1].size > r.base)
                throw std::invalid_argument("tlm_bus: overlapping regions");
        }
    }
};
Model::Model(sc_core::sc_module_name name, Config c) : sc_module(name), impl_(std::make_unique<Impl>(c)) {
    targets.register_b_transport(this, &Model::transport);
    targets.register_get_direct_mem_ptr(this, &Model::dmi);
    targets.register_transport_dbg(this, &Model::debug);
}
Model::~Model() = default;
bool Model::idle() const { return impl_->gate.idle(); }
const sc_core::sc_event& Model::idle_event() const { return impl_->gate.idle_event(); }
void Model::request_drain() { impl_->gate.drain(); }
bool Model::resume() { return impl_->gate.resume(); }
void Model::end_of_elaboration() {
    for (const auto& r : impl_->config.regions)
        if (r.target >= initiators.size()) SC_REPORT_ERROR(name(), "region target has no bound initiator");
}
bool Model::dmi(int, tlm::tlm_generic_payload& tx, tlm::tlm_dmi&) { tx.set_dmi_allowed(false); return false; }
unsigned Model::debug(int, tlm::tlm_generic_payload&) { return 0; }
void Model::transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
    tx.set_dmi_allowed(false);
    const auto address = tx.get_address();
    const Region* region = nullptr;
    for (const auto& r : impl_->config.regions)
        if (address >= r.base && address - r.base < r.size &&
            tx.get_data_length() <= r.size - (address - r.base)) { region = &r; break; }
    if (!region) { tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
    const auto process = sc_core::sc_get_current_process_handle();
    if (!process.valid() || process.proc_kind() == sc_core::SC_METHOD_PROC_) {
        tx.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return;
    }
    aix::esl::BlockingLease lease(impl_->gate);
    if (!lease) { tx.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
    if (delay != sc_core::SC_ZERO_TIME) { sc_core::wait(delay); delay = sc_core::SC_ZERO_TIME; }
    impl_->server.lock();
    struct Guard {
        sc_core::sc_mutex& mutex; tlm::tlm_generic_payload& tx; std::uint64_t address;
        ~Guard() { tx.set_address(address); mutex.unlock(); }
    } guard{impl_->server, tx, address};
    if (impl_->config.route_latency != sc_core::SC_ZERO_TIME) sc_core::wait(impl_->config.route_latency);
    tx.set_address(address - region->base);
    initiators[region->target]->b_transport(tx, delay);
    if (delay != sc_core::SC_ZERO_TIME) { sc_core::wait(delay); delay = sc_core::SC_ZERO_TIME; }
    tx.set_dmi_allowed(false);
}
}
