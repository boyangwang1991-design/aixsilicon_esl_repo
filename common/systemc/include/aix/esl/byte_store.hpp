#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aix::esl {
// Storage only: no SystemC time, arbitration, or global instance state.
class ByteStore {
public:
    explicit ByteStore(std::size_t size) : data_(size, 0) {}
    bool contains(std::uint64_t address, std::size_t length) const noexcept {
        return address <= data_.size() && length <= data_.size() - address;
    }
    void clear() { std::fill(data_.begin(), data_.end(), 0); }
    unsigned char& operator[](std::size_t offset) { return data_.at(offset); }
    const unsigned char& operator[](std::size_t offset) const { return data_.at(offset); }
private:
    std::vector<unsigned char> data_;
};
}
