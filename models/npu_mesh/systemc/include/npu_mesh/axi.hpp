#pragma once
#include <npu_mesh/model.hpp>
#include <deque>
namespace aix::esl::npu_mesh {
struct AxiAddress {
    std::uint64_t address = 0;
    unsigned id = 0, beats = 1, beat_bytes = 16;
    bool incr = true, exclusive = false, device = false;
    std::uint64_t token = 0;
};
struct AxiWriteBeat {std::vector<unsigned char> data, strobes;bool last=false;};
struct AxiB {unsigned id;Status status;};
struct AxiR {unsigned id;Status status;bool last;std::vector<unsigned char> data;};
// Transaction/beat handshake abstraction, not a pin-level RTL AXI VIP.
class AxiNiu final : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(AxiNiu);
    AxiNiu(sc_core::sc_module_name,Model&,unsigned source,unsigned context=0,unsigned bus_bytes=128,unsigned depth=8);
    ~AxiNiu() override;
    bool aw(const AxiAddress&); // false = backpressure; no acceptance
    bool w(const AxiWriteBeat&); // No WID; binds accepted AW FIFO, W-before-AW backpressured
    bool ar(const AxiAddress&);
    bool b(AxiB&);
    bool r(AxiR&);
    bool idle() const;
private:
    struct Impl;std::unique_ptr<Impl> p;void run();
};
}
