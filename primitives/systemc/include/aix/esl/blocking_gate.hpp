#pragma once
#include <aix/esl/activity_monitor.hpp>
#include <systemc>
#include <cstddef>
#include <stdexcept>
namespace aix::esl {
class BlockingGate {
public:
    explicit BlockingGate(std::size_t capacity, bool counters = true)
        : capacity_(capacity), monitor_(counters) {
        if (!capacity) throw std::invalid_argument("max_outstanding must be positive");
    }
    BlockingGate(const BlockingGate&) = delete;
    BlockingGate& operator=(const BlockingGate&) = delete;
    bool enter() {
        if (!open_ || count_ == capacity_) { monitor_.rejected(); return false; }
        ++count_; monitor_.accepted(count_); return true;
    }
    void leave() {
        if (!count_) throw std::logic_error("unbalanced BlockingGate::leave");
        --count_; monitor_.removed(count_);
        if (!count_) idle_event_.notify(sc_core::SC_ZERO_TIME);
    }
    void drain() { open_ = false; }
    bool idle() const { return count_ == 0; }
    bool accepting() const { return open_; }
    std::size_t outstanding() const { return count_; }
    std::size_t capacity() const { return capacity_; }
    // Free credits do not imply admission when drain has closed the gate.
    std::size_t available() const { return capacity_ - count_; }
    bool resume() { if (!idle()) return false; open_ = true; return true; }
    const sc_core::sc_event& idle_event() const { return idle_event_; }
    ActivityStats stats() const { return monitor_.snapshot(); }
private:
    std::size_t capacity_, count_ = 0;
    bool open_ = true;
    sc_core::sc_event idle_event_;
    ActivityMonitor monitor_;
};
// Acquires one credit and releases it exactly once; gate must outlive the lease.
class BlockingLease {
public:
    explicit BlockingLease(BlockingGate& gate) : gate_(gate), owns_(gate.enter()) {}
    ~BlockingLease() { release(); }
    explicit operator bool() const { return owns_; }
    void release() { if (owns_) { gate_.leave(); owns_ = false; } }
    BlockingLease(const BlockingLease&) = delete;
    BlockingLease& operator=(const BlockingLease&) = delete;
private:
    BlockingGate& gate_;
    bool owns_;
};
}
