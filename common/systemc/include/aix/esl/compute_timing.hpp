#pragma once
#include <aix/esl/resource_timing.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace aix::esl {
// Work units are chosen by the owner (e.g. MACs, bytes, elements). This is an
// occupancy model, not an algorithm implementation or a hardware area estimate.
struct ComputeTiming {
    sc_core::sc_time period;
    std::uint64_t units_per_cycle;
    std::uint64_t pipeline_cycles = 0;
    std::uint64_t initiation_cycles = 1;
    unsigned instances = 1;
    unsigned capacity = 1;

    sc_core::sc_time latency(std::uint64_t work_units) const {
        validate();
        if (!work_units) throw std::invalid_argument("compute work must be positive");
        const auto cycles = 1 + (work_units - 1) / units_per_cycle;
        if (pipeline_cycles > std::numeric_limits<std::uint64_t>::max() - cycles)
            throw std::overflow_error("compute cycle count");
        return duration(cycles + pipeline_cycles);
    }
    ResourceTiming resource(std::uint64_t work_units) const {
        const auto delay = latency(work_units);
        return ResourceTiming(delay, duration(initiation_cycles), instances, capacity);
    }
private:
    void validate() const {
        if (period == sc_core::SC_ZERO_TIME || !units_per_cycle || !initiation_cycles || !instances || !capacity)
            throw std::invalid_argument("compute timing geometry");
    }
    sc_core::sc_time duration(std::uint64_t cycles) const {
        if (cycles > std::numeric_limits<std::uint64_t>::max() / period.value())
            throw std::overflow_error("compute duration");
        return sc_core::sc_time::from_value(cycles * period.value());
    }
};
}
