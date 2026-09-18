#pragma once
#include <systemc>
#include <cstdint>
namespace aix::esl::{{name}} {
struct Config { std::uint32_t initial_value = 0; sc_core::sc_time access_latency = sc_core::sc_time(1, sc_core::SC_NS); };
}
