#include <irq_controller/model.hpp>
#include <aix/esl/mmio32.hpp>
#include <stdexcept>
namespace aix::esl::irq_controller {
namespace {
unsigned validated(unsigned count) {
    if (!count || count > 32) throw std::invalid_argument("irq_controller: sources must be 1..32");
    return count;
}
}
struct Model::Impl {
    aix::esl::Mmio32 mmio;
    sc_core::sc_event changed;
    std::uint32_t pending = 0, enable = 0, mask;
    explicit Impl(Config c) : mmio(c.access_latency), mask(c.sources == 32 ? UINT32_MAX : (1u << c.sources) - 1) {}
};
Model::Model(sc_core::sc_module_name n, Config c) : sc_module(n),
    sources("sources", validated(c.sources)), impl_(std::make_unique<Impl>(c)) {
    registers.register_b_transport(this, &Model::transport);
    registers.register_get_direct_mem_ptr(this, &Model::dmi);
    registers.register_transport_dbg(this, &Model::debug);
    SC_METHOD(handle_reset); sensitive << reset;
    SC_METHOD(update_irq); sensitive << impl_->changed << reset;
    for (auto& source : sources) sensitive << source;
}
Model::~Model() = default;
bool Model::idle() const { return impl_->mmio.idle(); }
const sc_core::sc_event& Model::idle_event() const { return impl_->mmio.idle_event(); }
void Model::request_drain() { impl_->mmio.drain(); }
bool Model::resume() { return !reset.read() && impl_->mmio.resume(); }
std::uint32_t Model::raw() const {
    std::uint32_t bits = 0;
    for (unsigned i = 0; i < sources.size(); ++i) if (sources[i].read()) bits |= 1u << i;
    return bits;
}
void Model::handle_reset() {
    impl_->mmio.reset(reset.read());
    if (reset.read()) { impl_->pending = 0; impl_->enable = 0; }
    impl_->changed.notify(sc_core::SC_ZERO_TIME);
}
void Model::update_irq() {
    if (!reset.read()) impl_->pending |= raw();
    irq.write(!reset.read() && (impl_->pending & impl_->enable));
}
bool Model::dmi(int, tlm::tlm_generic_payload& tx, tlm::tlm_dmi&) { tx.set_dmi_allowed(false); return false; }
unsigned Model::debug(int, tlm::tlm_generic_payload&) { return 0; }
void Model::transport(int, tlm::tlm_generic_payload& tx, sc_core::sc_time& delay) {
    impl_->mmio.access(tx, delay, [&](std::uint64_t address, bool write, std::uint32_t& value) {
        auto& p = *impl_;
        if (reset.read()) return tlm::TLM_GENERIC_ERROR_RESPONSE;
        p.pending |= raw();
        if (write) {
            switch (address) {
            case reg::pending:
                if (value & ~p.mask) return tlm::TLM_COMMAND_ERROR_RESPONSE;
                p.pending = (p.pending & ~value) | raw(); break;
            case reg::enable:
                if (value & ~p.mask) return tlm::TLM_COMMAND_ERROR_RESPONSE;
                p.enable = value; break;
            case reg::raw: case reg::priority: return tlm::TLM_COMMAND_ERROR_RESPONSE;
            default: return tlm::TLM_ADDRESS_ERROR_RESPONSE;
            }
        } else {
            switch (address) {
            case reg::pending: value = p.pending; break;
            case reg::enable: value = p.enable; break;
            case reg::raw: value = raw(); break;
            case reg::priority:
                value = reg::no_interrupt;
                for (unsigned i = 0; i < sources.size(); ++i)
                    if ((p.pending & p.enable) & (1u << i)) { value = i; break; }
                break;
            default: return tlm::TLM_ADDRESS_ERROR_RESPONSE;
            }
        }
        p.changed.notify(sc_core::SC_ZERO_TIME);
        return tlm::TLM_OK_RESPONSE;
    });
}
}
