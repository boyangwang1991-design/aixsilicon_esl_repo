#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace aix::esl::npu_sram_controller {
struct Region {
    uint64_t base=0, length=0, local_base=0;
    std::vector<unsigned> banks;
    std::string policy="modulo";
    unsigned stripe=32, shift=0, rotation=0;
};
struct Config {
    unsigned ports=8, banks=32, word_bytes=32, stripe_bytes=32, groups=4;
    uint64_t capacity=8388608, cycle_limit=2000000;
    std::string mapping="modulo", topology="flat", queue="voq", arbitration="rr";
    unsigned xor_shift=0, group_first=0, local_xor=0;
    unsigned dual_port=0, dual_ingress=0, full_data=0;
    unsigned read_latency=2, write_latency=1, bank_ii=1;
    unsigned outstanding=32, ids=16, ingress_entries=128, bank_entries=8;
    unsigned rob_beats=128, w_beats=128, completion_entries=8, rmw_contexts=4;
    unsigned lanes=4, return_bytes=128, remote_bytes=128, link_entries=16;
    unsigned link_buffer_bytes=512, link_latency=1, credit_delay=1, matching_rounds=4;
    unsigned age_guard=256, max_read_grants=16, ecc_bytes=0, ecc_lanes=4, ecc_ii=1;
    unsigned ecc_latency=1, ecc_group=0, macro_word_write=0, scrub_interval=0;
    unsigned correction_latency=1, trace_limit=20000;
    std::vector<Region> regions;
    void validate() const;
    static Config read(const std::string& path);
};
}
