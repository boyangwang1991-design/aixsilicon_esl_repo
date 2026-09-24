#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <npu_sram_controller/config.hpp>
namespace aix::esl::npu_mesh {
enum class Status { ok, retry, decode, denied, unsupported, target, corrupt, aborted, stale, timeout };
enum class Op { read, write, fence };
const char* status_name(Status);
struct Endpoint {
    unsigned router = 0;
    std::uint64_t base = 0, size = 65536;
    unsigned bytes_per_cycle = 32, latency = 8, slots = 8;
    std::uint64_t contexts = ~std::uint64_t(0);
    bool tile = false;
    std::string backend = "ddr";
    unsigned read_latency = 0, write_latency = 0, turnaround_cycles = 0;
    npu_sram_controller::Config sram;
    unsigned priority_context = 64, age_cycles = 128, issue_limit = 0;
};
struct Config {
    unsigned columns = 4, rows = 2, link_bytes = 32, control_bytes = 8;
    unsigned vcs_per_vn = 1, depth = 8, link_latency = 1, credit_latency = 2;
    unsigned router_latency = 1, packet_bytes = 256, outstanding = 16;
    unsigned return_bytes = 65536, command_depth = 8, command_latency = 2;
    unsigned timeout_cycles = 100000, max_transfer = 4096;
    unsigned fragment_window = 1, dma_window = 1;
    unsigned shape_bytes_per_cycle = 0, shape_burst_bytes = 4096, shape_context_min = 2;
    bool split = false, centralized = false, trace = true;
    std::vector<Endpoint> endpoints;
    void validate() const;
};
struct Request {
    Op op = Op::read;
    unsigned source = 0, context = 0, id = 0;
    std::uint64_t address = 0;
    unsigned length = 0;
    std::vector<unsigned char> data, enables;
    std::uint64_t token = 0; // Required for tile writes; trusted reservation handle.
    bool ordered = true; // AXI requests stay ordered; disjoint DMA children may overlap.
};
struct Submission { Status status; std::uint64_t handle = 0; };
struct Completion {
    std::uint64_t handle = 0, epoch = 0, accepted = 0, visible = 0, done = 0;
    unsigned source = 0, context = 0, id = 0, bytes = 0;
    Status status = Status::ok;
    bool uncertain = false;
    std::vector<unsigned char> data;
    std::vector<std::pair<unsigned,unsigned>> completed_ranges;
};
struct Metrics {
    std::uint64_t cycles = 0, accepted = 0, completed = 0, useful_bytes = 0;
    std::uint64_t link_bytes = 0, injected_bytes = 0, header_bytes = 0, mask_bytes = 0;
    std::uint64_t credit_stalls = 0, allocation_stalls = 0, endpoint_stalls = 0;
    std::uint64_t admission_stalls = 0, target_bytes = 0, late_packets = 0;
    std::uint64_t shape_stalls = 0, shaped_bytes = 0;
    unsigned occupancy_high = 0;
    unsigned fragment_peak = 0, return_reserved_peak = 0;
};
struct Trace {
    std::uint64_t cycle, handle;
    std::string module, event;
    unsigned source, context;
    unsigned bytes = 0;
};
struct PortMetrics {
    unsigned router, output, vn;
    std::uint64_t flits=0, bytes=0, credit_stalls=0, allocation_stalls=0, endpoint_stalls=0;
};
struct DmaSegment { std::uint64_t source, destination; unsigned bytes; std::uint64_t token = 0; };
struct DmaDescriptor {
    unsigned source = 0, context = 0, id = 0;
    std::vector<DmaSegment> segments;
};
struct DmaCompletion {
    std::uint64_t handle; Status status; unsigned bytes; std::vector<bool> completed;
    std::vector<unsigned> bytes_per_destination;
    bool uncertain = false;
};
}
