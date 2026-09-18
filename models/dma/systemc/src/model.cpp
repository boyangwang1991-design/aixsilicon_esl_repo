#include <dma/model.hpp>
#include <algorithm>
#include <aix/esl/bounded_queue.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>
namespace aix::esl::dma {
struct Model::Impl {
    Config config;
    aix::esl::BoundedQueue<Command> commands;
    aix::esl::BoundedQueue<Completion> completions;
    std::set<std::uint64_t> ids;
    sc_core::sc_event wake, completed, became_idle;
    std::uint64_t epoch = 0;
    bool open = true, active = false;
    explicit Impl(Config c) : config(c), commands(c.capacity), completions(c.capacity) {
        if (!c.capacity || c.capacity > 65535 || !c.burst_bytes ||
            !c.max_transfer_bytes || c.burst_bytes > c.max_transfer_bytes)
            throw std::invalid_argument("dma: capacity 1..65535; 0 < burst_bytes <= max_transfer_bytes");
    }
    void finish(Completion result) {
        if (!completions.try_push(result)) throw std::logic_error("DMA completion credit invariant");
        completed.notify(sc_core::SC_ZERO_TIME);
    }
};
Model::Model(sc_core::sc_module_name name, Config config)
    : sc_module(name), impl_(std::make_unique<Impl>(config)) { SC_THREAD(run); }
Model::~Model() = default;
Submit Model::submit(Command c) {
    auto& p = *impl_;
    if (!p.open) return Submit::closed;
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (!c.bytes || c.bytes > p.config.max_transfer_bytes ||
        c.source > max - (c.bytes - 1) || c.destination > max - (c.bytes - 1) ||
        (c.source <= c.destination ? c.destination - c.source < c.bytes : c.source - c.destination < c.bytes))
        return Submit::invalid;
    if (p.ids.count(c.id)) return Submit::duplicate;
    if (p.ids.size() == p.config.capacity) return Submit::full;
    p.ids.insert(c.id);
    if (!p.commands.try_push(c)) throw std::logic_error("DMA command credit invariant");
    p.wake.notify(sc_core::SC_ZERO_TIME);
    return Submit::accepted;
}
bool Model::pop_completion(Completion& result) {
    auto& p = *impl_;
    if (p.completions.empty()) return false;
    result = p.completions.front(); p.completions.pop(); p.ids.erase(result.id);
    return true;
}
const sc_core::sc_event& Model::completion_event() const { return impl_->completed; }
bool Model::idle() const { return !impl_->active && impl_->commands.empty(); }
const sc_core::sc_event& Model::idle_event() const { return impl_->became_idle; }
void Model::request_drain() { impl_->open = false; }
bool Model::resume() { if (!idle()) return false; impl_->open = true; return true; }
void Model::reset() {
    auto& p = *impl_; p.open = false; ++p.epoch;
    while (!p.commands.empty()) {
        p.finish({p.commands.front().id, Status::cancelled, 0, tlm::TLM_INCOMPLETE_RESPONSE});
        p.commands.pop();
    }
    if (idle()) p.became_idle.notify(sc_core::SC_ZERO_TIME);
}
void Model::run() {
    auto& p = *impl_;
    while (true) {
        if (p.commands.empty()) { sc_core::wait(p.wake); continue; }
        const auto c = p.commands.front(); p.commands.pop(); p.active = true;
        const auto epoch = p.epoch;
        Completion result{c.id, Status::completed, 0, tlm::TLM_OK_RESPONSE};
        std::vector<unsigned char> buffer(std::min(c.bytes, p.config.burst_bytes));
        auto transfer = [&](tlm::tlm_command direction, std::uint64_t address, unsigned bytes) {
            tlm::tlm_generic_payload tx;
            tx.set_command(direction); tx.set_address(address); tx.set_data_ptr(buffer.data());
            tx.set_data_length(bytes); tx.set_streaming_width(bytes);
            tx.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            auto delay = sc_core::SC_ZERO_TIME;
            memory->b_transport(tx, delay);
            if (delay != sc_core::SC_ZERO_TIME) sc_core::wait(delay);
            return tx.get_response_status();
        };
        while (result.bytes_written < c.bytes) {
            const auto n = std::min(p.config.burst_bytes, c.bytes - result.bytes_written);
            result.response = transfer(tlm::TLM_READ_COMMAND, c.source + result.bytes_written, n);
            if (epoch != p.epoch) { result.status = Status::cancelled; break; }
            if (result.response != tlm::TLM_OK_RESPONSE) { result.status = Status::failed; break; }
            result.response = transfer(tlm::TLM_WRITE_COMMAND, c.destination + result.bytes_written, n);
            if (result.response == tlm::TLM_OK_RESPONSE) result.bytes_written += n;
            if (epoch != p.epoch) { result.status = Status::cancelled; break; }
            if (result.response != tlm::TLM_OK_RESPONSE) { result.status = Status::failed; break; }
        }
        p.active = false; p.finish(result);
        if (idle()) p.became_idle.notify(sc_core::SC_ZERO_TIME);
    }
}
}
