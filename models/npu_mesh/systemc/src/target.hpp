#pragma once
#include "network.hpp"
#include <npu_sram_controller/model.hpp>
#include <aix/esl/byte_store.hpp>
namespace aix::esl::npu_mesh {
// Owns the only memory image and memory service time for this endpoint.
class MemoryTarget {
    struct Work {
        std::shared_ptr<Packet> packet;
        std::uint64_t due=0, native=0, aligned=0, arrived=0, sequence=0;
        unsigned prefix=0, beats=0, pushed=0, port=0;
        bool error=false;
        std::vector<unsigned char> data;
    };
    Endpoint cfg;
    std::unique_ptr<aix::esl::ByteStore> store;
    std::unique_ptr<npu_sram_controller::Model> sram;
    std::deque<Work> waiting;
    std::map<std::uint64_t,Work> issued;
    std::vector<std::deque<std::uint64_t>> writes;
    std::deque<std::shared_ptr<Packet>> complete;
    std::uint64_t sequence=0, next_issue=0, serviced=0, turnarounds=0, retries=0;
    bool last_write=false, have_last=false;
    unsigned queue_peak=0;
    std::shared_ptr<Packet> response(const std::shared_ptr<Packet>&) const;
public:
    explicit MemoryTarget(const Endpoint&, unsigned index);
    void accept(std::shared_ptr<Packet>, std::uint64_t now);
    void tick(std::uint64_t now, bool stalled, const std::function<bool(std::uint64_t)>& stopped);
    bool pop(std::shared_ptr<Packet>&);
    // Queued work can be discarded; accepted native SRAM AW/W must drain.
    std::vector<std::shared_ptr<Packet>> reset_pending();
    bool idle() const;
    void initialize(std::uint64_t, const std::vector<unsigned char>&);
    std::vector<unsigned char> inspect(std::uint64_t,unsigned) const;
    void report(std::ostream&) const;
    bool same_service(const Endpoint&) const;
};
}
