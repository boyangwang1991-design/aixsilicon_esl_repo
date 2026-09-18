#pragma once
#include <systemc>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>
namespace aix::esl {
// A grant commits service. Invoke only when the destination has capacity.
class Arbiter {
public:
    enum class Policy { round_robin, weighted_round_robin, priority };
    Arbiter(unsigned count, Policy policy=Policy::round_robin,
            std::vector<unsigned> weights={}, sc_core::sc_time age_guard=sc_core::SC_ZERO_TIME)
        : count_(count),policy_(policy),weights_(std::move(weights)),age_guard_(age_guard),since_(count) {
        if (!count) throw std::invalid_argument("arbiter count");
        if (weights_.empty()) weights_.assign(count,1);
        if (weights_.size()!=count || std::find(weights_.begin(),weights_.end(),0)!=weights_.end())
            throw std::invalid_argument("arbiter weights");
        remaining_=weights_[0];
    }
    std::optional<unsigned> grant(const std::vector<bool>& ready) {
        if (ready.size()!=count_) throw std::invalid_argument("arbiter ready width");
        auto now=sc_core::sc_time_stamp();
        std::optional<unsigned> oldest;
        for(unsigned i=0;i<count_;++i) {
            if (!ready[i]) {since_[i].reset();continue;}
            if (!since_[i]) since_[i]=now;
            if (age_guard_!=sc_core::SC_ZERO_TIME && now-*since_[i]>=age_guard_ &&
                (!oldest || *since_[i]<*since_[*oldest])) oldest=i;
        }
        std::optional<unsigned> chosen=oldest;
        if (!chosen) for(unsigned rank=0;rank<count_;++rank) {
            unsigned i=policy_==Policy::priority ? rank : (cursor_+rank)%count_;
            if (ready[i]) {chosen=i;break;}
        }
        if (!chosen) return std::nullopt;
        auto i=*chosen; since_[i]=now;
        if (policy_==Policy::weighted_round_robin) {
            if (i!=cursor_) remaining_=weights_[i];
            cursor_=i;
            if (--remaining_==0) {cursor_=(i+1)%count_;remaining_=weights_[cursor_];}
        } else {cursor_=(i+1)%count_;remaining_=weights_[cursor_];}
        return chosen;
    }
private:
    unsigned count_,cursor_=0,remaining_;Policy policy_;std::vector<unsigned> weights_;
    sc_core::sc_time age_guard_;std::vector<std::optional<sc_core::sc_time>> since_;
};
}
