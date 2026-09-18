#pragma once
#include <cstdint>
namespace aix::esl::dma {
struct Config {
    unsigned capacity = 4; // Credits include queued, active and unconsumed completions.
    unsigned burst_bytes = 64;
    unsigned max_transfer_bytes = 1048576;
};
struct Command { std::uint64_t id, source, destination; unsigned bytes; };
enum class Submit { accepted, closed, full, invalid, duplicate };
enum class Status { completed, failed, cancelled };
}
