#pragma once
#include <aix/esl/statistics.hpp>
#include <systemc>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace aix::esl {
struct ActivityStats {
    bool enabled = true, overflow = false;
    std::uint64_t accepted = 0, removed = 0, rejected = 0, discarded = 0;
    std::uint64_t occupancy_ticks = 0, elapsed_ticks = 0;
    std::size_t level = 0, high_watermark = 0;
};
// Passive, integer SystemC tick accounting shared by queues and admission gates.
class ActivityMonitor {
public:
    explicit ActivityMonitor(bool enabled = true)
        : start_(sc_core::sc_time_stamp().value()), last_(start_) { stats_.enabled = enabled; }
    void accepted(std::size_t level) { change(level); if(stats_.enabled) add(stats_, stats_.accepted, 1); }
    void removed(std::size_t level) { change(level); if(stats_.enabled) add(stats_, stats_.removed, 1); }
    void rejected() { if(stats_.enabled) add(stats_, stats_.rejected, 1); }
    void discarded(std::size_t count, std::size_t level) {
        change(level); if(stats_.enabled) add(stats_, stats_.discarded, count);
    }
    ActivityStats snapshot() const {
        auto result = stats_;
        if (result.enabled) {
            integrate(result, sc_core::sc_time_stamp().value() - last_);
            result.elapsed_ticks = sc_core::sc_time_stamp().value() - start_;
        }
        return result;
    }
private:
    ActivityStats stats_;
    std::uint64_t start_, last_;
    static void add(ActivityStats& s, std::uint64_t& target, std::uint64_t value) {
        detail::saturating_add(target, value, s.overflow);
    }
    static void integrate(ActivityStats& s, std::uint64_t ticks) {
        const auto max = std::numeric_limits<std::uint64_t>::max();
        if (s.level && ticks > max / s.level) { s.occupancy_ticks = max; s.overflow = true; }
        else add(s, s.occupancy_ticks, ticks * s.level);
    }
    void change(std::size_t level) {
        if (!stats_.enabled) return;
        const auto now = sc_core::sc_time_stamp().value();
        integrate(stats_, now - last_); last_ = now;
        stats_.level = level; stats_.high_watermark = std::max(stats_.high_watermark, level);
    }
};
}
