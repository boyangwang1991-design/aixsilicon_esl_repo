#pragma once
#include "config.hpp"
#include <vector>
#include <memory>
namespace aix::esl { class RegionMapper; }
namespace aix::esl::npu_sram_controller {
struct Location { unsigned bank; uint64_t local; };
struct Slice {
    unsigned bank; uint64_t word;
    std::vector<unsigned> offsets, lanes;
};
class Mapper {
    Config c_;
    std::shared_ptr<const ::aix::esl::RegionMapper> regions_;
public:
    explicit Mapper(const Config& c);
    Location map(uint64_t address) const;
    uint64_t inverse(unsigned bank,uint64_t local) const;
    std::vector<Slice> split(uint64_t address,unsigned bytes) const;
};
}
