#pragma once
#include <aix/esl/address_mapper.hpp>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>
namespace aix::esl {
// Logical windows map bijectively onto disjoint windows in ordered bank subsets.
// Holes are explicit; there is no fallback mapping or implicit storage allocation.
class RegionMapper {
public:
    struct Region {
        uint64_t base, length, local_base;
        std::vector<unsigned> banks;
        unsigned stripe;
        AddressMapper::Policy policy = AddressMapper::Policy::interleaved;
        unsigned xor_shift = 0, rotation = 0;
    };
    struct Location {
        std::size_t region;
        unsigned bank, group;
        uint64_t local, row, offset;
    };
    RegionMapper(uint64_t logical_capacity, unsigned banks, uint64_t bank_capacity,
                 unsigned groups, std::vector<Region> regions)
        : capacity_(logical_capacity), banks_(banks), bank_capacity_(bank_capacity), groups_(groups),
          regions_(std::move(regions)) {
        if (!capacity_ || !banks_ || !bank_capacity_ || !groups_ || banks_ % groups_ || regions_.empty())
            throw std::invalid_argument("region table geometry");
        using Range = std::pair<uint64_t, uint64_t>;
        std::vector<Range> logical;
        std::map<unsigned, std::vector<Range>> physical;
        for (const auto& region : regions_) {
            if (region.banks.empty() || region.banks.size() > std::numeric_limits<unsigned>::max() ||
                region.base > capacity_ || region.length > capacity_ - region.base ||
                region.rotation >= region.banks.size()) throw std::invalid_argument("region range/bank subset/rotation");
            // Reuse the public flat mapper's geometry, policy and XOR validation.
            mappings_.emplace_back(region.length, static_cast<unsigned>(region.banks.size()),
                                   region.stripe, 1, region.policy, region.xor_shift);
            const auto extent = region.length / region.banks.size();
            if (region.local_base > bank_capacity_ || extent > bank_capacity_ - region.local_base)
                throw std::invalid_argument("region physical capacity");
            logical.emplace_back(region.base, region.base + region.length);
            std::set<unsigned> unique;
            for (auto bank : region.banks) {
                if (bank >= banks_ || !unique.insert(bank).second) throw std::invalid_argument("region duplicate/invalid bank");
                physical[bank].emplace_back(region.local_base, region.local_base + extent);
            }
        }
        const auto no_overlap = [](std::vector<Range>& ranges) {
            std::sort(ranges.begin(), ranges.end());
            for (std::size_t i = 1; i < ranges.size(); ++i)
                if (ranges[i].first < ranges[i - 1].second) throw std::invalid_argument("region alias");
        };
        no_overlap(logical);
        for (auto& item : physical) no_overlap(item.second);
    }
    Location map(uint64_t address) const {
        if (address >= capacity_) throw std::out_of_range("logical address");
        for (std::size_t i = 0; i < regions_.size(); ++i) {
            const auto& region = regions_[i];
            if (address < region.base || address - region.base >= region.length) continue;
            const auto mapped = mappings_[i].map(address - region.base);
            const auto index = (uint64_t(mapped.bank) + region.rotation) % region.banks.size();
            const auto bank = region.banks[index];
            return {i, bank, bank / (banks_ / groups_), region.local_base + mapped.local,
                    mapped.row, mapped.local % region.stripe};
        }
        throw std::out_of_range("logical region hole");
    }
    uint64_t inverse(unsigned bank, uint64_t local) const {
        if (bank >= banks_ || local >= bank_capacity_) throw std::out_of_range("physical address");
        for (std::size_t i = 0; i < regions_.size(); ++i) {
            const auto& region = regions_[i];
            if (local < region.local_base || local - region.local_base >= region.length / region.banks.size()) continue;
            const auto it = std::find(region.banks.begin(), region.banks.end(), bank);
            if (it == region.banks.end()) continue;
            const auto index = (uint64_t(it - region.banks.begin()) + region.banks.size() - region.rotation) % region.banks.size();
            return region.base + mappings_[i].inverse(static_cast<unsigned>(index), local - region.local_base);
        }
        throw std::out_of_range("physical region hole");
    }
private:
    uint64_t capacity_;
    unsigned banks_;
    uint64_t bank_capacity_;
    unsigned groups_;
    std::vector<Region> regions_;
    std::vector<AddressMapper> mappings_;
};
}
