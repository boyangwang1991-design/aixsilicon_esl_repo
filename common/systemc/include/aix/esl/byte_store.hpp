#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace aix::esl {
// Storage only: no time, arbitration, payload retention, or shared global state.
class ByteStore {
public:
    explicit ByteStore(std::size_t size, const std::vector<unsigned char>& initial = {}) : data_(size, 0) {
        if (!size || initial.size() > size) throw std::invalid_argument("invalid ByteStore size/initial image");
        std::copy(initial.begin(), initial.end(), data_.begin());
    }
    bool contains(std::uint64_t address, std::size_t length) const noexcept {
        return address <= data_.size() && length <= data_.size() - address;
    }
    bool read(std::uint64_t address, unsigned char* output, std::size_t length,
              const unsigned char* enables = nullptr, std::size_t enable_length = 0) const {
        if (!output || !valid(address, length, enables, enable_length)) return false;
        for (std::size_t i = 0; i < length; ++i)
            if (!enables || enables[i % enable_length]) output[i] = data_[address + i];
        return true;
    }
    bool write(std::uint64_t address, const unsigned char* input, std::size_t length,
               const unsigned char* enables = nullptr, std::size_t enable_length = 0) {
        if (!input || !valid(address, length, enables, enable_length)) return false;
        for (std::size_t i = 0; i < length; ++i)
            if (!enables || enables[i % enable_length]) data_[address + i] = input[i];
        return true;
    }
    void clear() { std::fill(data_.begin(), data_.end(), 0); }
    std::size_t size() const { return data_.size(); }
    unsigned char& operator[](std::size_t offset) { return data_.at(offset); }
    const unsigned char& operator[](std::size_t offset) const { return data_.at(offset); }
private:
    std::vector<unsigned char> data_;
    bool valid(std::uint64_t address, std::size_t length, const unsigned char* be, std::size_t n) const {
        if (!length || !contains(address, length)) return false;
        if (!be) return n == 0;
        if (!n) return false;
        for (std::size_t i = 0; i < n; ++i) if (be[i] != 0 && be[i] != 0xff) return false;
        return true;
    }
};
}
