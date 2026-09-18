#include <aix/esl/byte_store.hpp>
#include <aix/esl/sparse_store.hpp>
#include <aix/esl/ecc_memory.hpp>
#include <aix/esl/resource_timing.hpp>
#include <systemc>
#include <array>
#include <iostream>
#include <limits>
#include <string>
using namespace aix::esl;
using namespace sc_core;

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "missing rejection");
}
template<class Store> void storage_contract() {
    Store store(8192, {9, 8, 7});
    std::array<unsigned char, 12> input{}, output{};
    for (unsigned i = 0; i < input.size(); ++i) input[i] = i + 1;
    unsigned char mask[] = {0xff, 0, 0xff};
    check(store.write(4091, input.data(), input.size(), mask, 3), "cross-page masked write");
    output.fill(99);
    check(store.read(4091, output.data(), output.size(), mask, 3), "cross-page masked read");
    for (unsigned i = 0; i < input.size(); ++i)
        check(output[i] == (i % 3 == 1 ? 99 : i + 1), "disabled read lane changed");
    check(store.read(4091, output.data(), output.size()), "full read");
    for (unsigned i = 0; i < input.size(); ++i)
        check(output[i] == (i % 3 == 1 ? 0 : i + 1), "masked write oracle");
    const auto before = output;
    unsigned char invalid[] = {0xff, 1};
    check(!store.write(4091, input.data(), input.size(), invalid, 2), "invalid mask admitted");
    check(!store.write(8190, input.data(), input.size()), "overflow range admitted");
    check(!store.read(UINT64_MAX, output.data(), 2), "wrapped address admitted");
    check(!store.read(0, output.data(), 0), "empty transfer admitted");
    check(store.read(4091, output.data(), output.size()) && output == before, "rejection mutated bytes");
    check(store.contains(8192, 0) && !store.contains(8192, 1), "range endpoint");
    store.clear();
    store.read(4091, output.data(), output.size());
    for (auto byte : output) check(byte == 0, "clear");
}
template<class Store> void ecc_contract() {
    using Memory = EccMemory<Store>;
    using Status = typename Memory::Status;
    Memory memory(64);
    constexpr std::uint64_t original = 0x1122334455667788ULL;
    auto full = memory.write_word(0, original);
    check(full.committed && full.cost.reads == 0 && full.cost.writes == 1 && full.cost.encodes == 1, "full-write cost");
    memory.inject_bit(0, 17);
    auto corrected = memory.read_word(0);
    check(corrected.status == Status::corrected && corrected.data == original && corrected.cost.writes == 0, "read correction");
    check(memory.read_word(0).status == Status::corrected, "read silently scrubbed");
    auto scrub = memory.scrub_word(0);
    check(scrub.status == Status::corrected && scrub.committed && scrub.cost.reads == 1 && scrub.cost.decodes == 1 &&
          scrub.cost.writes == 1 && scrub.cost.encodes == 1, "scrub repair cost");
    check(memory.read_word(0).status == Status::clean, "scrub did not repair physical codeword");
    check(!memory.scrub_word(0).committed, "clean scrub wrote memory");
    memory.inject_bit(0, 70); // parity error must also be repaired by an RMW
    auto partial = memory.write_word(0, 0xaabbccddeeff0011ULL, 0x81);
    check(partial.committed && partial.status == Status::corrected && partial.data == 0xaa22334455667711ULL, "RMW byte preservation");
    check(partial.cost.reads == 1 && partial.cost.writes == 1 && partial.cost.decodes == 1 && partial.cost.encodes == 1, "RMW cost");
    memory.inject_bit(0, 4); memory.inject_bit(0, 25);
    auto blocked = memory.write_word(0, 0, 1);
    check(blocked.status == Status::uncorrectable && !blocked.committed && blocked.cost.writes == 0, "uncorrectable RMW overwritten");
    check(memory.scrub_word(0).status == Status::uncorrectable, "uncorrectable scrub overwritten");
    memory.inject_bit(0, 4); memory.inject_bit(0, 25);
    check(memory.read_word(0).data == partial.data, "failed RMW changed stored data");
    memory.inject_bit(0, 3); memory.inject_bit(0, 9);
    check(memory.write_word(0, 42).committed && memory.read_word(0).data == 42, "full replacement of bad word");
    auto no_op = memory.write_word(0, 99, 0);
    check(!no_op.committed && no_op.cost.reads == 0 && memory.read_word(0).data == 42, "all-disabled write");
    rejects([&]{ memory.read_word(8); });
    rejects([&]{ memory.inject_bit(0, 72); });
    rejects([]{ Memory bad(9); });
    memory.clear();
    check(memory.statistics().cost.reads == 0 && memory.read_word(0).data == 0, "ECC clear");
}

// A composing owner explicitly serializes each codeword's complete RMW sequence.
// Bank and codec resources are shared by demand traffic and scrub; no delay lives
// inside the functional ECC service. Operations become visible at completion.
template<class Store> struct TimedOwner : sc_module {
    EccMemory<Store> memory{64};
    ResourceTiming bank{sc_time(2, SC_NS), sc_time(2, SC_NS), 1, 1};
    ResourceTiming codec{sc_time(1, SC_NS), sc_time(1, SC_NS), 1, 1};
    sc_mutex word_lock;
    bool enable_scrub, scrub_done = false, demand_done = false;
    SC_HAS_PROCESS(TimedOwner);
    TimedOwner(sc_module_name name, bool scrub) : sc_module(name), enable_scrub(scrub) {
        memory.write_word(0, 0x1122334455667788ULL);
        memory.inject_bit(0, 20);
        SC_THREAD(scrubber);
        SC_THREAD(demand);
    }
    void service(ResourceTiming& resource) {
        auto ticket = resource.reserve();
        check(bool(ticket), "exclusive owner overcommitted resource");
        wait(ticket->ready - sc_time_stamp());
        resource.retire(ticket->id);
    }
    void scrubber() {
        if (enable_scrub) {
            word_lock.lock();
            service(bank); service(codec);
            auto word = memory.read_word(0);
            check(word.status == Secded64::Status::corrected, "injected scrub error");
            service(codec); service(bank);
            memory.write_word(0, word.data);
            check(sc_time_stamp() == sc_time(6, SC_NS), "scrub analytical finish");
            word_lock.unlock();
        }
        scrub_done = true;
    }
    void demand() {
        wait(1, SC_NS);
        word_lock.lock();
        service(bank); service(codec);
        auto word = memory.read_word(0);
        check(word.status != Secded64::Status::uncorrectable, "demand decode");
        service(codec); service(bank);
        memory.write_word(0, (word.data & ~std::uint64_t(0xff)) | 0xab);
        word_lock.unlock();
        check(sc_time_stamp() == sc_time(enable_scrub ? 12 : 7, SC_NS), "shared scrub interference");
        const auto stats = memory.statistics();
        check(stats.cost.reads == (enable_scrub ? 2u : 1u) && stats.cost.writes == (enable_scrub ? 3u : 2u), "timed operation accounting");
        check(memory.read_word(0).data == 0x11223344556677abULL, "scrub lost demand update");
        check(bank.outstanding() == 0 && codec.outstanding() == 0, "resource drain");
        demand_done = true;
        sc_stop();
    }
};
int sc_main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        const std::string mode = argv[1];
        if (mode == "dense") { storage_contract<ByteStore>(); ecc_contract<ByteStore>(); }
        else if (mode == "sparse") {
            storage_contract<SparseStore>(); ecc_contract<SparseStore>();
            SparseStore large(std::uint64_t(1) << 40);
            unsigned char value = 7, disabled = 0;
            check(large.write(8191, &value, 1, &disabled, 1) && large.allocated_pages() == 0, "disabled write allocated page");
            check(large.write((std::uint64_t(1) << 40) - 1, &value, 1), "40-bit end address");
        } else if (mode == "timed_dense" || mode == "timed_baseline") {
            TimedOwner<ByteStore> owner("owner", mode == "timed_dense");
            sc_start(); check(owner.scrub_done && owner.demand_done, "unfinished timed owner");
        } else if (mode == "timed_sparse") {
            TimedOwner<SparseStore> owner("owner", true);
            sc_start(); check(owner.scrub_done && owner.demand_done, "unfinished sparse owner");
        } else return 2;
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
