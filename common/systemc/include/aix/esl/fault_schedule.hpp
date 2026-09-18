#pragma once
#include <systemc>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace aix::esl {
// Explicit half-open target-time windows; query has no RNG or hidden side effects.
// Owner applies the result at the documented admission/service/completion boundary.
class FaultSchedule {
public:
    struct Window {std::string target;sc_core::sc_time begin,end;bool pause=false,error=false;
        sc_core::sc_time extra_latency;unsigned bandwidth_percent=100;};
    struct Effect{bool pause=false,error=false;sc_core::sc_time extra_latency;unsigned bandwidth_percent=100;};
    explicit FaultSchedule(std::vector<Window> windows):windows_(std::move(windows)){
        for(const auto& w:windows_)if(w.target.empty()||w.end<=w.begin||w.bandwidth_percent>100)
            throw std::invalid_argument("fault window");
        // Delay windows cannot overlap for a target: avoid ambiguous additive policy.
        for(size_t i=0;i<windows_.size();++i)for(size_t j=0;j<i;++j){const auto& a=windows_[i];const auto& b=windows_[j];
            if(a.target==b.target&&a.begin<b.end&&b.begin<a.end)throw std::invalid_argument("overlapping target faults");}
    }
    Effect at(const std::string& target,sc_core::sc_time time=sc_core::sc_time_stamp())const{
        for(const auto& w:windows_)if(w.target==target&&w.begin<=time&&time<w.end)return {w.pause,w.error,w.extra_latency,w.bandwidth_percent};
        return {};
    }
private:std::vector<Window> windows_;
};
}
