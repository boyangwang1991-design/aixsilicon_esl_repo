#include <aix/esl/region_mapper.hpp>
#include <aix/esl/byte_store.hpp>
#include <aix/esl/sparse_store.hpp>
#include <systemc>
#include <iostream>
#include <set>
using namespace aix::esl;
using Policy = AddressMapper::Policy;
using Region = RegionMapper::Region;
static void check(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
template<class Error, class F> static void rejected(F action) {
    bool caught = false;
    try { action(); } catch (const Error&) { caught = true; }
    check(caught, "expected mapping rejection");
}
static std::vector<Region> regions() {
    // Deliberately unsorted logical regions; adjacent windows may share banks.
    return {{128, 64, 16, {3, 1}, 4, Policy::xor_interleaved, 1, 1},
            {0, 64, 0, {0, 2}, 8, Policy::contiguous},
            {64, 32, 32, {0, 2}, 4, Policy::interleaved}};
}
// Fixed reference wiring, independent of both mapper implementations.
static std::pair<unsigned, uint64_t> expected(unsigned address) {
    if (address < 64) return {address < 32 ? 0u : 2u, address % 32};
    if (address < 96) return {((address - 64) / 4) % 2 ? 2u : 0u, 32 + (address - 64) / 8 * 4 + address % 4};
    const unsigned wiring[] = {1, 3, 1, 3, 3, 1, 3, 1, 1, 3, 1, 3, 3, 1, 3, 1};
    return {wiring[(address - 128) / 4], 16 + (address - 128) / 8 * 4 + address % 4};
}
static bool mapped(unsigned address) { return address < 96 || (address >= 128 && address < 192); }

static void bijection() {
    RegionMapper mapper(256, 4, 64, 2, regions());
    std::set<std::pair<unsigned, uint64_t>> visited;
    for (unsigned address = 0; address < 256; ++address) {
        if (!mapped(address)) { rejected<std::out_of_range>([&] { mapper.map(address); }); continue; }
        const auto p = mapper.map(address);
        check(std::make_pair(p.bank, p.local) == expected(address), "fixed wiring oracle");
        check(p.group == p.bank / 2 && p.region == (address < 64 ? 1u : address < 96 ? 2u : 0u), "group/region identity");
        const auto base = address < 64 ? 0u : address < 96 ? 32u : 16u;
        const auto stripe = address < 64 ? 8u : 4u;
        check(p.row == (p.local - base) / stripe && p.offset == (p.local - base) % stripe, "relative row/offset");
        check(mapper.inverse(p.bank, p.local) == address && visited.insert({p.bank, p.local}).second, "bijective mapping");
    }
    check(visited.size() == 160, "mapped byte count");
    for (unsigned bank = 0; bank < 4; ++bank) for (uint64_t local = 0; local < 64; ++local)
        if (!visited.count({bank, local})) rejected<std::out_of_range>([&] { mapper.inverse(bank, local); });
    rejected<std::out_of_range>([&] { mapper.map(256); });
    rejected<std::out_of_range>([&] { mapper.inverse(4, 0); });
    rejected<std::out_of_range>([&] { mapper.inverse(0, 64); });
}

template<class Store> static void storage() {
    RegionMapper mapper(256, 4, 64, 2, regions());
    std::vector<Store> banks;
    for (unsigned b = 0; b < 4; ++b) banks.emplace_back(64);
    std::vector<std::vector<unsigned char>> oracle(4, std::vector<unsigned char>(64));
    for (unsigned address = 0; address < 256; ++address) if (mapped(address)) {
        const auto p = mapper.map(address);
        const unsigned char value = address + 1, mask = address % 3 ? 0xff : 0;
        check(banks[p.bank].write(p.local, &value, 1, &mask, 1), "mapped byte write");
        const auto physical = expected(address);
        if (mask) oracle[physical.first][physical.second] = value;
    }
    // Check every physical byte, including unused banks/windows, without inverse().
    for (unsigned bank = 0; bank < 4; ++bank) for (unsigned local = 0; local < 64; ++local) {
        unsigned char value = 255;
        check(banks[bank].read(local, &value, 1) && value == oracle[bank][local], "independent storage/mask oracle");
    }
}

static void errors() {
    const auto bad = [](std::vector<Region> table) {
        rejected<std::invalid_argument>([&] { RegionMapper mapper(256, 4, 64, 2, table); });
    };
    bad({});
    for (unsigned fault = 0; fault < 10; ++fault) {
        auto table = regions();
        auto& r = table[0];
        switch (fault) {
        case 0: r.base = 32; break; // logical alias
        case 1: r.banks = {0, 2}; r.local_base = 0; break; // physical alias
        case 2: r.banks = {1, 1}; break;
        case 3: r.banks = {4, 1}; break;
        case 4: r.local_base = 63; break;
        case 5: r.base = UINT64_MAX; break;
        case 6: r.length = UINT64_MAX; break;
        case 7: r.rotation = 2; break;
        case 8: r.xor_shift = 64; break;
        case 9: r.policy = static_cast<Policy>(99); break;
        }
        bad(table);
    }
    bad({{0, 48, 0, {0, 1, 2}, 4, Policy::xor_interleaved}});
    bad({{0, 0, 0, {0}, 1}});
    bad({{0, 4, 0, {}, 1}});
    bad({{0, 4, 0, {0}, 0}});
    rejected<std::invalid_argument>([] { RegionMapper m(256, 4, 64, 3, regions()); });
    // Contiguous/interleaved support non-power-of-two subsets.
    RegionMapper thirds(48, 3, 16, 1, {{0, 48, 0, {2, 0, 1}, 4}});
    check(thirds.map(0).bank == 2 && thirds.map(4).bank == 0 && thirds.map(8).bank == 1, "three-bank subset");
}
static void wide() {
    const uint64_t base = UINT64_MAX - 63, local = UINT64_MAX - 127;
    RegionMapper mapper(UINT64_MAX, 1, UINT64_MAX, 1, {{base, 63, local, {0}, 1}});
    const auto p = mapper.map(UINT64_MAX - 1);
    check(p.local == local + 62 && mapper.inverse(0, p.local) == UINT64_MAX - 1, "64-bit boundary mapping");
    rejected<std::out_of_range>([&] { mapper.map(UINT64_MAX); });
    rejected<std::invalid_argument>([&] { RegionMapper bad(UINT64_MAX, 1, UINT64_MAX, 1, {{base, 64, local, {0}, 1}}); });
    rejected<std::invalid_argument>([&] { RegionMapper bad(UINT64_MAX, 1, UINT64_MAX, 1, {{0, 64, UINT64_MAX - 63, {0}, 1}}); });
}
int sc_main(int argc, char** argv) {
    try {
        check(argc == 2, "usage: regions bijection|dense|sparse|errors|wide");
        const std::string mode = argv[1];
        if (mode == "bijection") bijection();
        else if (mode == "dense") storage<ByteStore>();
        else if (mode == "sparse") storage<SparseStore>();
        else if (mode == "errors") errors();
        else if (mode == "wide") wide();
        else throw std::invalid_argument("unknown region fixture");
        check(sc_core::sc_time_stamp() == sc_core::SC_ZERO_TIME, "mapper advanced target time");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
