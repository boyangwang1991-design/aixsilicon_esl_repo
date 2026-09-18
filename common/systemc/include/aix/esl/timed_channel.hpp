#pragma once
#include <systemc>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
namespace aix::esl {
// Finite store-and-forward link. Bandwidth serialization and pipeline latency
// are independent. Credits return after consumption plus a configured delay.
template<class T> class TimedChannel {
public:
    TimedChannel(unsigned width,size_t capacity,sc_core::sc_time period,
                 sc_core::sc_time latency,sc_core::sc_time credit_delay=sc_core::SC_ZERO_TIME)
        :width_(width),capacity_(capacity),period_(period),latency_(latency),credit_delay_(credit_delay){
        if(!width||!capacity||period==sc_core::SC_ZERO_TIME)throw std::invalid_argument("channel geometry");
    }
    bool send(const T& value,unsigned bytes){
        if(!bytes)throw std::invalid_argument("empty packet");reclaim();
        auto now=sc_core::sc_time_stamp();if(items_.size()+credits_.size()==capacity_||now<next_)return false;
        uint64_t cycles=1+(uint64_t(bytes)-1)/width_;
        if(cycles>UINT64_MAX/period_.value())throw std::overflow_error("channel serialization");
        uint64_t serial=cycles*period_.value();
        if(serial>UINT64_MAX-now.value()||latency_.value()>UINT64_MAX-now.value()-serial)
            throw std::overflow_error("channel timestamp");
        auto end=sc_core::sc_time::from_value(now.value()+serial);
        items_.push_back({value,end+latency_});next_=end;return true;
    }
    bool receive(T& value){
        auto now=sc_core::sc_time_stamp();if(items_.empty()||items_.front().ready>now)return false;
        if(credit_delay_.value()>UINT64_MAX-now.value())throw std::overflow_error("credit timestamp");
        value=items_.front().value;credits_.push_back(now+credit_delay_);items_.pop_front();reclaim();return true;
    }
    size_t outstanding(){reclaim();return items_.size()+credits_.size();}
    void reset(){reclaim();if(!items_.empty()||!credits_.empty())throw std::logic_error("channel reset requires drain");next_=sc_core::sc_time_stamp();}
private:
    struct Item{T value;sc_core::sc_time ready;};unsigned width_;size_t capacity_;
    sc_core::sc_time period_,latency_,credit_delay_,next_;
    std::deque<Item> items_;std::deque<sc_core::sc_time> credits_;
    void reclaim(){while(!credits_.empty()&&credits_.front()<=sc_core::sc_time_stamp())credits_.pop_front();}
};
// Transaction-level CDC visibility, not a metastability or physical CDC model.
// Destination sampling edges are absolute multiples of its period from time zero.
template<class T> class ClockDomainQueue {
public:
    ClockDomainQueue(size_t capacity,sc_core::sc_time destination_period,unsigned synchronizer_cycles)
        :capacity_(capacity),period_(destination_period.value()),cycles_(synchronizer_cycles){
        if(!capacity||!period_||!cycles_)throw std::invalid_argument("CDC parameters");
    }
    bool push(const T& value){
        if(items_.size()==capacity_)return false;auto now=sc_core::sc_time_stamp().value();
        uint64_t edge=now/period_+(now%period_!=0);
        if(cycles_>UINT64_MAX-edge || edge+cycles_>UINT64_MAX/period_)throw std::overflow_error("CDC visibility timestamp");
        items_.push_back({value,(edge+cycles_)*period_});return true;
    }
    bool pop(T& value){if(items_.empty()||items_.front().ready>sc_core::sc_time_stamp().value())return false;
        value=items_.front().value;items_.pop_front();return true;}
    size_t reset(){auto dropped=items_.size();items_.clear();return dropped;}
private:struct Item{T value;uint64_t ready;};size_t capacity_;uint64_t period_;unsigned cycles_;std::deque<Item> items_;
};
}
