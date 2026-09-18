#pragma once
#include "config.hpp"
#include <vector>
namespace aix::esl::npu_sram_controller {
struct Location { unsigned bank; uint64_t local; };
struct Slice {
    unsigned bank; uint64_t word;
    std::vector<unsigned> offsets, lanes;
};
class Mapper {
    Config c_;
public:
    explicit Mapper(const Config& c):c_(c) { c_.validate(); }
    Location map(uint64_t address) const;
    uint64_t inverse(unsigned bank,uint64_t local) const;
    std::vector<Slice> split(uint64_t address,unsigned bytes) const;
};
}
