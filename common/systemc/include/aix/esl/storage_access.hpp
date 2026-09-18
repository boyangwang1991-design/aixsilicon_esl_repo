#pragma once
#include <cstddef>
#include <cstdint>

namespace aix::esl {
// Shared functional storage contract. Buffers must not alias the backing store.
inline bool storage_contains(std::uint64_t capacity, std::uint64_t address,
                             std::size_t length) noexcept {
    return address <= capacity && length <= capacity - address;
}
inline bool storage_access_valid(std::uint64_t capacity, std::uint64_t address,
                                 std::size_t length, const unsigned char* enables,
                                 std::size_t enable_length) noexcept {
    if (!length || !storage_contains(capacity, address, length)) return false;
    if (!enables) return enable_length == 0;
    if (!enable_length) return false;
    for (std::size_t i = 0; i < enable_length; ++i)
        if (enables[i] != 0 && enables[i] != 0xff) return false;
    return true;
}
}
