#pragma once
#include <systemc>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aix::esl::ram {
enum class Profile { functional, resource };
struct Config {
    std::size_t capacity_bytes = 4096;
    std::size_t max_outstanding = 4; // Includes waiting AND serving callers.
    std::uint64_t setup_ticks = 1;
    std::uint64_t bytes_per_tick = 8;
    sc_core::sc_time tick = sc_core::sc_time(1, sc_core::SC_NS);
    Profile profile = Profile::resource;
    bool read_only = false;
    std::vector<unsigned char> initial_data;
    bool enable_counters = true;
};
struct Counters {
    bool enabled = true;
    std::uint64_t accepted = 0, completed = 0, cancelled = 0, rejected = 0;
    std::uint64_t read_bytes = 0, write_bytes = 0;
};
}
