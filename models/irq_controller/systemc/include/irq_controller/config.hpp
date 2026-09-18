#pragma once
#include <systemc>
#include <cstdint>
namespace aix::esl::irq_controller {
struct Config {
    unsigned sources = 4;
    sc_core::sc_time access_latency = sc_core::sc_time(1, sc_core::SC_NS);
};
namespace reg {
constexpr std::uint64_t pending = 0x00, enable = 0x04, raw = 0x08, priority = 0x0c;
constexpr std::uint32_t no_interrupt = 32;
}
}
