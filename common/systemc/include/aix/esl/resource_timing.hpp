#pragma once
#include <systemc>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>
namespace aix::esl {
// Passive reservation ledger. The caller schedules completion in SystemC.
// Credits include completed results until explicitly retired by the consumer.
class ResourceTiming {
public:
    struct Ticket { uint64_t id; unsigned instance; sc_core::sc_time ready; };
    ResourceTiming(sc_core::sc_time latency, sc_core::sc_time interval,
                   unsigned instances, unsigned capacity)
        : latency_(latency), interval_(interval), capacity_(capacity), next_(instances) {
        if (!instances || !capacity || interval == sc_core::SC_ZERO_TIME)
            throw std::invalid_argument("resource instances/capacity/II");
    }
    std::optional<Ticket> reserve() {
        const auto now = sc_core::sc_time_stamp();
        if (active_.size() == capacity_) return std::nullopt;
        auto it = std::min_element(next_.begin(), next_.end());
        if (*it > now) return std::nullopt;
        if (serial_ == std::numeric_limits<uint64_t>::max() ||
            latency_.value() > std::numeric_limits<uint64_t>::max() - now.value() ||
            interval_.value() > std::numeric_limits<uint64_t>::max() - now.value())
            throw std::overflow_error("resource timestamp/id");
        Ticket ticket{serial_, static_cast<unsigned>(it-next_.begin()), now+latency_};
        active_.push_back(ticket); // no state change if allocation fails
        ++serial_; *it = now+interval_;
        return ticket;
    }
    void retire(uint64_t id) {
        auto it = std::find_if(active_.begin(),active_.end(),[id](const Ticket& t){return t.id==id;});
        if (it == active_.end()) throw std::logic_error("unknown or duplicate retirement");
        if (it->ready > sc_core::sc_time_stamp()) throw std::logic_error("premature retirement");
        active_.erase(it);
    }
    size_t outstanding() const { return active_.size(); }
    // Reset is only legal after drain: no silent cancellation of work owned elsewhere.
    void reset() {
        if (!active_.empty()) throw std::logic_error("resource reset requires drain");
        std::fill(next_.begin(), next_.end(), sc_core::sc_time_stamp());
    }
private:
    sc_core::sc_time latency_, interval_;
    unsigned capacity_;
    uint64_t serial_=0;
    std::vector<sc_core::sc_time> next_;
    std::vector<Ticket> active_;
};
}
