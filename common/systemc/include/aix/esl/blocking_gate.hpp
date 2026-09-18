#pragma once
#include <systemc>
#include <cstddef>
#include <stdexcept>
namespace aix::esl {
class BlockingGate {
public:
    explicit BlockingGate(std::size_t capacity) : capacity_(capacity) {
        if (!capacity) throw std::invalid_argument("max_outstanding must be positive");
    }
    bool enter() {
        if (!open_ || count_ == capacity_) return false;
        ++count_; return true;
    }
    void leave() { if (--count_ == 0) idle_event_.notify(sc_core::SC_ZERO_TIME); }
    void drain() { open_ = false; }
    bool idle() const { return count_ == 0; }
    bool resume() { if (!idle()) return false; open_ = true; return true; }
    const sc_core::sc_event& idle_event() const { return idle_event_; }
private:
    std::size_t capacity_, count_ = 0;
    bool open_ = true;
    sc_core::sc_event idle_event_;
};
class BlockingLease {
public:
    explicit BlockingLease(BlockingGate& gate) : gate_(gate) {}
    ~BlockingLease() { gate_.leave(); }
    BlockingLease(const BlockingLease&) = delete;
    BlockingLease& operator=(const BlockingLease&) = delete;
private:
    BlockingGate& gate_;
};
}
