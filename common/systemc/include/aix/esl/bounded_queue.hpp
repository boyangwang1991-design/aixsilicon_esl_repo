#pragma once
#include <aix/esl/activity_monitor.hpp>
#include <deque>
#include <stdexcept>
namespace aix::esl {
// Capacity counts stored records only. In-service ownership belongs to the caller.
template<class T> class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity, bool counters = true)
        : capacity_(capacity), monitor_(counters) {
        if (!capacity) throw std::invalid_argument("queue capacity must be positive");
    }
    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    bool try_push(const T& value) {
        if (full()) { monitor_.rejected(); return false; }
        values_.push_back(value); monitor_.accepted(size());
        changed_.notify(sc_core::SC_ZERO_TIME); return true;
    }
    bool try_pop(T& result) {
        if (empty()) return false;
        result = values_.front(); // If copying throws, leave the stored item intact.
        pop(); return true;
    }
    const T& front() const {
        if (empty()) throw std::out_of_range("front of empty queue");
        return values_.front();
    }
    bool pop() {
        if (empty()) return false;
        values_.pop_front(); monitor_.removed(size());
        changed_.notify(sc_core::SC_ZERO_TIME); return true;
    }
    void clear() {
        if (empty()) return;
        auto n = size(); values_.clear(); monitor_.discarded(n, 0);
        changed_.notify(sc_core::SC_ZERO_TIME);
    }
    bool empty() const { return values_.empty(); }
    bool full() const { return size() == capacity_; }
    std::size_t size() const { return values_.size(); }
    std::size_t capacity() const { return capacity_; }
    const sc_core::sc_event& changed_event() const { return changed_; }
    ActivityStats stats() const { return monitor_.snapshot(); }
private:
    const std::size_t capacity_;
    std::deque<T> values_;
    ActivityMonitor monitor_;
    sc_core::sc_event changed_;
};
}
