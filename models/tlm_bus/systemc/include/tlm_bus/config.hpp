#pragma once
#include <systemc>
#include <cstdint>
#include <cstddef>
#include <vector>
namespace aix::esl::tlm_bus {
struct Region { std::uint64_t base, size; unsigned target; };
struct Config {
    std::vector<Region> regions;
    std::size_t max_outstanding = 4;
    sc_core::sc_time route_latency = sc_core::sc_time(1, sc_core::SC_NS);
};
}
