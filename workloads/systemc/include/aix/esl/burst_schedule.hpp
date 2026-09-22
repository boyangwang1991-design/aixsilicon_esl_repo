#pragma once
#include <cstdint>
#include <stdexcept>
namespace aix::esl {
// Finite temporal batches of ordinary transactions, not an AXI burst protocol.
// Release times never depend on acceptance: an owner retains rejected requests.
class BurstSchedule {
public:
    BurstSchedule(std::uint64_t requests, std::uint64_t burst_requests,
                  std::uint64_t period_cycles, std::uint64_t phase_cycles = 0)
        : count_(requests), burst_(burst_requests), period_(period_cycles), phase_(phase_cycles) {
        if (!count_ || !burst_) throw std::invalid_argument("burst request count/size");
        const auto last = (count_ - 1) / burst_;
        if (last && period_ > (UINT64_MAX - phase_) / last) throw std::overflow_error("burst release cycle");
    }
    std::uint64_t size() const { return count_; }
    std::uint64_t earliest(std::uint64_t index) const {
        if (index >= count_) throw std::out_of_range("burst request index");
        return phase_ + (index / burst_) * period_;
    }
    std::uint64_t total_bytes(std::uint64_t bytes_per_request) const {
        if (!bytes_per_request) throw std::invalid_argument("burst request width");
        if (bytes_per_request > UINT64_MAX / count_) throw std::overflow_error("burst byte count");
        return count_ * bytes_per_request;
    }
private:
    std::uint64_t count_, burst_, period_, phase_;
};
}
