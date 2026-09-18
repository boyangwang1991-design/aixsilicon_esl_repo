#pragma once
#include <systemc>
#include <cstdint>
namespace aix::esl::timer {
struct Config {
    sc_core::sc_time tick = sc_core::sc_time(1, sc_core::SC_NS);
    sc_core::sc_time access_latency = sc_core::sc_time(1, sc_core::SC_NS);
    std::uint32_t reset_reload = 10;
};
namespace reg {
constexpr std::uint64_t control = 0x00, reload = 0x04, remaining = 0x08, pending = 0x0c;
constexpr std::uint32_t enable = 1, periodic = 2, irq_enable = 4;
}
}
