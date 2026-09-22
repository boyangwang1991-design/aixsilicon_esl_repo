#pragma once
#include <aix/esl/deterministic_rng.hpp>
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
        // Canonical ordering gives deterministic export and O(n log n) validation.
        std::sort(windows_.begin(), windows_.end(), [](const Window& a, const Window& b) {
            return a.target < b.target || (a.target == b.target && a.begin < b.begin);
        });
        for(size_t i=1;i<windows_.size();++i)
            if(windows_[i].target==windows_[i-1].target && windows_[i].begin<windows_[i-1].end)
                throw std::invalid_argument("overlapping target faults");
    }
    Effect at(const std::string& target,sc_core::sc_time time=sc_core::sc_time_stamp())const{
        for(const auto& w:windows_)if(w.target==target&&w.begin<=time&&time<w.end)return {w.pause,w.error,w.extra_latency,w.bandwidth_percent};
        return {};
    }
    const std::vector<Window>& windows() const { return windows_; }
    // One independently jittered window in each fixed slot, generated once.
    static FaultSchedule seeded(const std::string& target, std::size_t count,
                                sc_core::sc_time slot, sc_core::sc_time duration,
                                uint64_t seed, const std::string& stream, Effect effect) {
        const auto width = slot.value(), length = duration.value();
        if (target.empty() || !width || !length || length > width || effect.bandwidth_percent > 100)
            throw std::invalid_argument("seeded fault geometry/effect");
        if (count > std::numeric_limits<uint64_t>::max() / width)
            throw std::overflow_error("seeded fault horizon");
        DeterministicRng rng(seed, stream);
        std::vector<Window> windows;
        windows.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto begin = i * width + rng.uniform(width - length + 1);
            windows.push_back({target, sc_core::sc_time::from_value(begin), sc_core::sc_time::from_value(begin + length),
                               effect.pause, effect.error, effect.extra_latency, effect.bandwidth_percent});
        }
        return FaultSchedule(std::move(windows));
    }
private:std::vector<Window> windows_;
};
}
