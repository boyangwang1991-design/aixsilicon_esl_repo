#pragma once
#include <aix/esl/storage_access.hpp>
#include <array>
#include <map>
#include <stdexcept>
#include <vector>

namespace aix::esl {
// Same burst contract as ByteStore; absent 4096-byte pages read as zero.
class SparseStore {
public:
    explicit SparseStore(std::uint64_t capacity,
                         const std::vector<unsigned char>& initial = {}) : capacity_(capacity) {
        if (!capacity || initial.size() > capacity)
            throw std::invalid_argument("invalid SparseStore size/initial image");
        if (!initial.empty()) write(0, initial.data(), initial.size());
    }
    std::uint64_t size() const noexcept { return capacity_; }
    bool contains(std::uint64_t address, std::size_t length) const noexcept {
        return storage_contains(capacity_, address, length);
    }
    unsigned char read(std::uint64_t address) const {
        bounds(address);
        const auto it = pages_.find(address / page_size);
        return it == pages_.end() ? 0 : it->second[address % page_size];
    }
    void write(std::uint64_t address, unsigned char value) {
        bounds(address);
        pages_[address / page_size][address % page_size] = value;
    }
    bool read(std::uint64_t address, unsigned char* output, std::size_t length,
              const unsigned char* enables = nullptr, std::size_t enable_length = 0) const {
        if (!output || !storage_access_valid(capacity_, address, length, enables, enable_length))
            return false;
        for (std::size_t i = 0; i < length; ++i)
            if (!enables || enables[i % enable_length]) output[i] = read(address + i);
        return true;
    }
    bool write(std::uint64_t address, const unsigned char* input, std::size_t length,
               const unsigned char* enables = nullptr, std::size_t enable_length = 0) {
        if (!input || !storage_access_valid(capacity_, address, length, enables, enable_length))
            return false;
        // Allocate first, so allocation failure cannot partially change stored bytes.
        // Newly allocated zero pages may remain after std::bad_alloc.
        for (std::size_t i = 0; i < length; ++i)
            if (!enables || enables[i % enable_length]) pages_.try_emplace((address + i) / page_size);
        for (std::size_t i = 0; i < length; ++i)
            if (!enables || enables[i % enable_length])
                pages_.at((address + i) / page_size)[(address + i) % page_size] = input[i];
        return true;
    }
    void clear() { pages_.clear(); }
    std::size_t allocated_pages() const noexcept { return pages_.size(); }
private:
    static constexpr std::uint64_t page_size = 4096;
    std::uint64_t capacity_;
    std::map<std::uint64_t, std::array<unsigned char, page_size>> pages_;
    void bounds(std::uint64_t address) const {
        if (address >= capacity_) throw std::out_of_range("sparse address");
    }
};
}
