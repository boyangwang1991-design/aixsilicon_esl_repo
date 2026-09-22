#pragma once
#include <aix/esl/fault_schedule.hpp>
#include <aix/esl/resource_timing.hpp>
#include <optional>
#include <vector>
namespace aix::esl {
// Single-issue fault-aware reservation adapter. The owner still owns payloads,
// executes effects at completion, and returns credit only after consuming results.
class FaultService {
public:
    struct Ticket { uint64_t id; sc_core::sc_time ready; bool error; };
    FaultService(sc_core::sc_time latency, sc_core::sc_time interval, unsigned capacity,
                 FaultSchedule schedule, std::string target)
        : resource_(latency, interval, 1, capacity), latency_(latency), interval_(interval),
          schedule_(std::move(schedule)), target_(std::move(target)) {
        if (target_.empty()) throw std::invalid_argument("fault service target");
        tickets_.reserve(capacity); // Push after resource admission cannot allocate.
    }
    FaultService(const FaultService&) = delete;
    FaultService& operator=(const FaultService&) = delete;
    std::optional<Ticket> reserve() {
        const auto now = sc_core::sc_time_stamp();
        const auto effect = schedule_.at(target_, now);
        if (effect.pause || !effect.bandwidth_percent || now < next_ || !resource_.available()) return std::nullopt;
        const auto max = std::numeric_limits<uint64_t>::max();
        const auto percent = effect.bandwidth_percent;
        // ceil(interval_ticks * 100 / percent), without overflowing the product.
        const auto quotient = interval_.value() / percent;
        const auto tail = ((interval_.value() % percent) * 100 + percent - 1) / percent;
        if (quotient > (max - tail) / 100) throw std::overflow_error("fault initiation interval");
        const auto spacing = quotient * 100 + tail;
        if (latency_.value() > max - now.value() ||
            effect.extra_latency.value() > max - now.value() - latency_.value() || spacing > max - now.value())
            throw std::overflow_error("fault service deadline");
        auto base = resource_.reserve();
        if (!base) return std::nullopt;
        Ticket ticket{base->id, base->ready + effect.extra_latency, effect.error};
        tickets_.push_back(ticket);
        next_ = sc_core::sc_time::from_value(now.value() + spacing);
        return ticket;
    }
    void retire(uint64_t id) {
        auto it = std::find_if(tickets_.begin(), tickets_.end(), [id](const Ticket& t) { return t.id == id; });
        if (it == tickets_.end() || it->ready > sc_core::sc_time_stamp())
            throw std::logic_error("unknown/duplicate/early fault retirement");
        resource_.retire(id); tickets_.erase(it);
    }
    std::size_t outstanding() const { return resource_.outstanding(); }
    std::size_t capacity() const { return resource_.capacity(); }
    std::size_t available() const { return resource_.available(); }
    void reset() { resource_.reset(); next_ = sc_core::sc_time_stamp(); }
private:
    ResourceTiming resource_;
    sc_core::sc_time latency_, interval_, next_;
    FaultSchedule schedule_;
    std::string target_;
    std::vector<Ticket> tickets_;
};
}
