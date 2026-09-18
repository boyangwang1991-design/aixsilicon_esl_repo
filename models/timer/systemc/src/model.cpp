#include <timer/model.hpp>
#include <aix/esl/mmio32.hpp>
#include <limits>
#include <stdexcept>
namespace aix::esl::timer {
struct Model::Impl {
    Config config;
    aix::esl::Mmio32 mmio;
    sc_core::sc_event changed, output_changed, became_idle;
    sc_core::sc_time deadline, last_fire;
    std::uint32_t control = 0, reload;
    bool pending = false, fired = false;
    explicit Impl(Config c) : config(c), mmio(c.access_latency), reload(c.reset_reload) {
        if (c.tick == sc_core::SC_ZERO_TIME || !c.reset_reload ||
            c.tick.value() > std::numeric_limits<std::uint64_t>::max() / UINT32_MAX)
            throw std::invalid_argument("timer: tick/reload zero or interval overflow");
    }
};
Model::Model(sc_core::sc_module_name n, Config c) : sc_module(n), impl_(std::make_unique<Impl>(c)) {
    registers.register_b_transport(this, &Model::transport);
    registers.register_get_direct_mem_ptr(this, &Model::dmi);
    registers.register_transport_dbg(this, &Model::debug);
    SC_THREAD(run);
    SC_METHOD(handle_reset); sensitive << reset;
    SC_METHOD(update_irq); sensitive << impl_->output_changed << impl_->mmio.idle_event();
}
Model::~Model() = default;
bool Model::idle() const { return impl_->mmio.idle() && !(impl_->control & reg::enable); }
const sc_core::sc_event& Model::idle_event() const { return impl_->became_idle; }
void Model::request_drain() {
    impl_->mmio.drain(); impl_->control &= ~reg::enable;
    impl_->changed.notify(sc_core::SC_ZERO_TIME); impl_->output_changed.notify(sc_core::SC_ZERO_TIME);
}
bool Model::resume() { return !reset.read() && impl_->mmio.resume(); }
void Model::handle_reset() {
    auto& p = *impl_; p.mmio.reset(reset.read());
    if (reset.read()) {
        p.control = 0; p.reload = p.config.reset_reload; p.pending = false; p.fired = false;
        p.changed.notify(sc_core::SC_ZERO_TIME);
    }
    p.output_changed.notify(sc_core::SC_ZERO_TIME);
}
void Model::update_irq() {
    irq.write(!reset.read() && impl_->pending && (impl_->control & reg::irq_enable));
    if (idle()) impl_->became_idle.notify(sc_core::SC_ZERO_TIME);
}
void Model::reschedule() {
    auto& p = *impl_;
    if (p.control & reg::enable) {
        auto duration = sc_core::sc_time::from_value(p.config.tick.value() * p.reload);
        if (duration.value() > std::numeric_limits<std::uint64_t>::max() - sc_core::sc_time_stamp().value())
            SC_REPORT_FATAL(name(), "timer deadline overflows SystemC time");
        p.deadline = sc_core::sc_time_stamp() + duration;
    }
    p.changed.notify(sc_core::SC_ZERO_TIME);
    p.output_changed.notify(sc_core::SC_ZERO_TIME);
}
void Model::settle_due() {
    auto& p = *impl_;
    if (!reset.read() && (p.control & reg::enable) && sc_core::sc_time_stamp() >= p.deadline) {
        p.pending = true; p.fired = true; p.last_fire = sc_core::sc_time_stamp();
        if (p.control & reg::periodic) reschedule();
        else p.control &= ~reg::enable;
        p.output_changed.notify(sc_core::SC_ZERO_TIME);
    }
}
void Model::run() {
    while (true) {
        if (reset.read() || !(impl_->control & reg::enable)) sc_core::wait(impl_->changed);
        else {
            if (impl_->deadline > sc_core::sc_time_stamp())
                sc_core::wait(impl_->deadline - sc_core::sc_time_stamp(), impl_->changed);
            settle_due();
        }
    }
}
bool Model::dmi(int, tlm::tlm_generic_payload& tx, tlm::tlm_dmi&) { tx.set_dmi_allowed(false); return false; }
unsigned Model::debug(int, tlm::tlm_generic_payload&) { return 0; }
void Model::transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
    impl_->mmio.access(tx, delay, [&](std::uint64_t address, bool write, std::uint32_t& value) {
        auto& p = *impl_;
        if (reset.read()) return tlm::TLM_GENERIC_ERROR_RESPONSE;
        settle_due();
        if (write) {
            switch (address) {
            case reg::control:
                if (value & ~7u) return tlm::TLM_COMMAND_ERROR_RESPONSE;
                if ((value & reg::enable) && !p.mmio.accepting()) return tlm::TLM_GENERIC_ERROR_RESPONSE;
                p.control = value; reschedule(); break;
            case reg::reload:
                if (!value) return tlm::TLM_COMMAND_ERROR_RESPONSE;
                p.reload = value; reschedule(); break;
            case reg::pending:
                if (value & ~1u) return tlm::TLM_COMMAND_ERROR_RESPONSE;
                // Expiry wins over W1C at the same physical timestamp.
                if ((value & 1) && !(p.fired && p.last_fire == sc_core::sc_time_stamp())) p.pending = false;
                break;
            case reg::remaining: return tlm::TLM_COMMAND_ERROR_RESPONSE;
            default: return tlm::TLM_ADDRESS_ERROR_RESPONSE;
            }
        } else {
            switch (address) {
            case reg::control: value = p.control; break;
            case reg::reload: value = p.reload; break;
            case reg::pending: value = p.pending; break;
            case reg::remaining: {
                auto delta = (p.control & reg::enable) ? (p.deadline - sc_core::sc_time_stamp()).value() : 0;
                value = delta / p.config.tick.value() + (delta % p.config.tick.value() != 0); break;
            }
            default: return tlm::TLM_ADDRESS_ERROR_RESPONSE;
            }
        }
        p.output_changed.notify(sc_core::SC_ZERO_TIME);
        return tlm::TLM_OK_RESPONSE;
    });
}
}
