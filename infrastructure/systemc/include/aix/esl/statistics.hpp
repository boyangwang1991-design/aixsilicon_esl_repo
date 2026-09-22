#pragma once
#include <systemc>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace aix::esl {
namespace detail {
inline void saturating_add(std::uint64_t& value, std::uint64_t increment, bool& overflow) {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (increment > max - value) { value = max; overflow = true; }
    else value += increment;
}
}

struct CounterStats { bool enabled = true, overflow = false; std::uint64_t value = 0; };
class Counter {
public:
    explicit Counter(bool enabled = true) { stats_.enabled = enabled; }
    void add(std::uint64_t value = 1) {
        if (stats_.enabled) detail::saturating_add(stats_.value, value, stats_.overflow);
    }
    CounterStats snapshot() const { return stats_; }
private:
    CounterStats stats_;
};

struct GaugeStats {
    bool enabled = true, overflow = false;
    std::uint64_t level = 0, minimum = 0, maximum = 0;
    std::uint64_t integral_ticks = 0, busy_ticks = 0, elapsed_ticks = 0;
};
// A nonnegative, piecewise-constant level over [construction, snapshot).
class Gauge {
public:
    explicit Gauge(std::uint64_t initial = 0, bool enabled = true)
        : start_(sc_core::sc_time_stamp().value()), last_(start_) {
        stats_.enabled = enabled;
        if (enabled) stats_.level = stats_.minimum = stats_.maximum = initial;
    }
    void set(std::uint64_t level) {
        if (!stats_.enabled) return;
        const auto now = sc_core::sc_time_stamp().value();
        integrate(stats_, now - last_); last_ = now;
        stats_.level = level;
        stats_.minimum = std::min(stats_.minimum, level);
        stats_.maximum = std::max(stats_.maximum, level);
    }
    GaugeStats snapshot() const {
        auto result = stats_;
        if (result.enabled) {
            integrate(result, sc_core::sc_time_stamp().value() - last_);
            result.elapsed_ticks = sc_core::sc_time_stamp().value() - start_;
        }
        return result;
    }
private:
    GaugeStats stats_;
    std::uint64_t start_, last_;
    static void integrate(GaugeStats& stats, std::uint64_t ticks) {
        if (!stats.level) return;
        detail::saturating_add(stats.busy_ticks, ticks, stats.overflow);
        if (ticks > std::numeric_limits<std::uint64_t>::max() / stats.level) {
            stats.integral_ticks = std::numeric_limits<std::uint64_t>::max(); stats.overflow = true;
        } else detail::saturating_add(stats.integral_ticks, ticks * stats.level, stats.overflow);
    }
};

struct SpanStats {
    bool enabled = true, overflow = false;
    std::uint64_t count = 0, total_ticks = 0, minimum_ticks = 0, maximum_ticks = 0;
};
// Aggregate completed intervals without retaining IDs or allocating live records.
// Overlapping intervals contribute separately; this is not a busy-time union.
class Span {
public:
    explicit Span(bool enabled = true) { stats_.enabled = enabled; }
    void record(sc_core::sc_time begin, sc_core::sc_time end) {
        if (end < begin || end > sc_core::sc_time_stamp()) throw std::invalid_argument("span interval");
        if (!stats_.enabled) return;
        const std::uint64_t ticks = (end - begin).value();
        stats_.minimum_ticks = stats_.count ? std::min(stats_.minimum_ticks, ticks) : ticks;
        stats_.maximum_ticks = std::max(stats_.maximum_ticks, ticks);
        detail::saturating_add(stats_.count, 1, stats_.overflow);
        detail::saturating_add(stats_.total_ticks, ticks, stats_.overflow);
    }
    SpanStats snapshot() const { return stats_; }
private:
    SpanStats stats_;
};
}
