#pragma once
#include <systemc>
#include <cstdint>
namespace aix::esl::uart {
struct Config {
    unsigned tx_depth = 4, rx_depth = 4;
    sc_core::sc_time frame_time = sc_core::sc_time(10, sc_core::SC_NS);
    sc_core::sc_time access_latency = sc_core::sc_time(1, sc_core::SC_NS);
};
namespace reg {
constexpr std::uint64_t data=0, status=4, control=8, levels=12, error=16;
constexpr std::uint32_t loopback=1, irq_rx=2, irq_tx_empty=4, irq_error=8;
}
}
