#include <ram/model.hpp>
#include <aix/esl/byte_store.hpp>
#include <limits>
#include <stdexcept>
#include <string>

namespace aix::esl::ram {
namespace {
Config validated(Config c) {
    if (!c.capacity_bytes || !c.max_outstanding || !c.bytes_per_tick || c.tick <= sc_core::SC_ZERO_TIME)
        throw std::invalid_argument("ram Config: capacity/max_outstanding/bytes_per_tick/tick must be positive");
    if (c.profile != Profile::functional && c.profile != Profile::resource)
        throw std::invalid_argument("ram Config: unsupported profile");
    if (c.initial_data.size() > c.capacity_bytes)
        throw std::invalid_argument("ram Config: initial image exceeds capacity");
    const auto beats = c.capacity_bytes / c.bytes_per_tick + (c.capacity_bytes % c.bytes_per_tick != 0);
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (c.setup_ticks > max - beats || c.setup_ticks + beats > max / c.tick.value())
        throw std::invalid_argument("ram Config: service time overflows SystemC time");
    return c;
}
}
struct Model::Impl {
    Config config;
    aix::esl::ByteStore store;
    sc_core::sc_mutex server;
    sc_core::sc_event reset_event, became_idle;
    std::size_t outstanding = 0;
    std::uint64_t epoch = 0;
    bool accepting = true;
    Counters counts;
    explicit Impl(Config c) : config(validated(c)), store(config.capacity_bytes) {
        counts.enabled = c.enable_counters;
        for (std::size_t i = 0; i < config.initial_data.size(); ++i) store[i] = config.initial_data[i];
    }
};
Model::Model(sc_core::sc_module_name name, Config config)
    : sc_module(name), impl_(std::make_unique<Impl>(config)) {
    memory.register_b_transport(this, &Model::transport);
    memory.register_get_direct_mem_ptr(this, &Model::dmi);
    memory.register_transport_dbg(this, &Model::debug);
}
Model::~Model() = default;
const Config& Model::config() const noexcept { return impl_->config; }
bool Model::idle() const noexcept { return impl_->outstanding == 0; }
const sc_core::sc_event& Model::idle_event() const noexcept { return impl_->became_idle; }
Counters Model::counters() const noexcept { return impl_->counts; }
void Model::request_drain() { impl_->accepting = false; }
bool Model::resume() {
    if (!idle()) return false;
    impl_->accepting = true;
    return true;
}
void Model::reset(bool clear_memory) {
    ++impl_->epoch;
    impl_->accepting = false;
    if (clear_memory) {
        impl_->store.clear();
        for (std::size_t i = 0; i < impl_->config.initial_data.size(); ++i)
            impl_->store[i] = impl_->config.initial_data[i];
    }
    impl_->reset_event.notify(sc_core::SC_ZERO_TIME);
}
bool Model::dmi(int, tlm::tlm_generic_payload& tx, tlm::tlm_dmi&) {
    tx.set_dmi_allowed(false);
    return false;
}
unsigned Model::debug(int, tlm::tlm_generic_payload&) { return 0; }

void Model::transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
    auto& p = *impl_;
    tx.set_dmi_allowed(false);
    auto reject = [&](tlm::tlm_response_status status) {
        tx.set_response_status(status);
        if (p.counts.enabled) ++p.counts.rejected;
    };
    const auto length = tx.get_data_length();
    const auto address = tx.get_address();
    if (!tx.is_read() && !tx.is_write()) { reject(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
    if (tx.is_write() && p.config.read_only) { reject(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
    if (!length || !tx.get_data_ptr()) { reject(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
    if (!p.store.contains(address, length)) { reject(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
    if (tx.get_streaming_width() < length) { reject(tlm::TLM_BURST_ERROR_RESPONSE); return; }
    const auto* enables = tx.get_byte_enable_ptr();
    const auto enable_length = tx.get_byte_enable_length();
    if (enables) {
        if (!enable_length) { reject(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return; }
        for (unsigned i = 0; i < enable_length; ++i)
            if (enables[i] != 0 && enables[i] != 0xff) {
                reject(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return;
            }
    }
    const auto process = sc_core::sc_get_current_process_handle();
    if (!process.valid() || process.proc_kind() == sc_core::SC_METHOD_PROC_ ||
        !p.accepting || p.outstanding >= p.config.max_outstanding) {
        reject(tlm::TLM_GENERIC_ERROR_RESPONSE); return;
    }
    const auto epoch = p.epoch;
    ++p.outstanding;
    if (p.counts.enabled) ++p.counts.accepted;
    const auto incoming = delay;
    delay = sc_core::SC_ZERO_TIME;
    // Local scope owns the blocking payload until return, including all waits.
    struct Completion {
        Impl& p;
        bool locked = false;
        ~Completion() {
            if (locked) p.server.unlock();
            if (--p.outstanding == 0) p.became_idle.notify(sc_core::SC_ZERO_TIME);
        }
    } completion{p};
    auto cancelled = [&]() {
        if (epoch == p.epoch) return false;
        tx.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        if (p.counts.enabled) ++p.counts.cancelled;
        return true;
    };
    // A reset notification can still be pending after an idle model resumes.
    // Only an epoch change cancels this request; a stale notification must not
    // shorten its incoming delay or service interval.
    auto wait_interval = [&](sc_core::sc_time duration) {
        const auto end = sc_core::sc_time_stamp() + duration;
        while (sc_core::sc_time_stamp() < end) {
            sc_core::wait(end - sc_core::sc_time_stamp(), p.reset_event);
            if (cancelled()) return false;
        }
        return !cancelled();
    };
    if (!wait_interval(incoming)) return;
    p.server.lock();
    completion.locked = true;
    if (cancelled()) return;
    if (p.config.profile == Profile::resource) {
        const auto beats = length / p.config.bytes_per_tick + (length % p.config.bytes_per_tick != 0);
        const auto ticks = p.config.setup_ticks + beats;
        if (!wait_interval(sc_core::sc_time::from_value(ticks * p.config.tick.value()))) return;
    }
    for (unsigned i = 0; i < length; ++i) {
        if (enables && enables[i % enable_length] == 0) continue;
        if (tx.is_write()) p.store[address + i] = tx.get_data_ptr()[i];
        else tx.get_data_ptr()[i] = p.store[address + i];
    }
    tx.set_response_status(tlm::TLM_OK_RESPONSE);
    if (p.counts.enabled) {
        ++p.counts.completed;
        (tx.is_write() ? p.counts.write_bytes : p.counts.read_bytes) += length;
    }
}
}
