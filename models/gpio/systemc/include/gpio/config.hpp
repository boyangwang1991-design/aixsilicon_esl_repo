#pragma once
#include <systemc>
#include <cstdint>
namespace aix::esl::gpio {
struct Config {
    unsigned width = 32;
    sc_core::sc_time access_latency = sc_core::sc_time(1, sc_core::SC_NS);
};
namespace reg {
constexpr std::uint64_t direction=0, output=4, input=8, irq_enable=12, pending=16;
}
}
