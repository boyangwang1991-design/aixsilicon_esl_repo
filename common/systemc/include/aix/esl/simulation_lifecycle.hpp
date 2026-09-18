#pragma once
#include <systemc>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
namespace aix::esl {
class SimulationLifecycle {
public:
    enum class Phase{initialize,warmup,measure,drain,finished};
    Phase phase()const{return phase_;}
    bool injecting()const{return phase_==Phase::warmup||phase_==Phase::measure;}
    void warmup(){transition(Phase::initialize,Phase::warmup);}
    void measure(){transition(Phase::warmup,Phase::measure);begin_=sc_core::sc_time_stamp();}
    void stop_injection(){transition(Phase::measure,Phase::drain);end_=sc_core::sc_time_stamp();}
    void finish(bool drained){if(!drained)throw std::logic_error("finish requires drain");transition(Phase::drain,Phase::finished);}
    sc_core::sc_time measurement_duration()const{if(phase_!=Phase::drain&&phase_!=Phase::finished)throw std::logic_error("measurement not closed");return end_-begin_;}
private:Phase phase_=Phase::initialize;sc_core::sc_time begin_,end_;
    void transition(Phase expected,Phase next){if(phase_!=expected)throw std::logic_error("lifecycle transition");phase_=next;}
};
// Explicit progress and declared timed waits distinguish deadlock from long service.
class ProgressWatchdog {
public:
    explicit ProgressWatchdog(sc_core::sc_time timeout):timeout_(timeout),progress_(sc_core::sc_time_stamp()){
        if(timeout==sc_core::SC_ZERO_TIME)throw std::invalid_argument("watchdog timeout");}
    void progress(){progress_=sc_core::sc_time_stamp();}
    void waiting(const std::string& owner,const std::string& reason,sc_core::sc_time until){
        if(owner.empty()||reason.empty()||until<sc_core::sc_time_stamp())throw std::invalid_argument("watchdog wait");
        waits_[owner]={reason,until};
    }
    void resumed(const std::string& owner){if(!waits_.erase(owner))throw std::logic_error("unknown wait owner");progress();}
    bool stalled()const{
        auto now=sc_core::sc_time_stamp(),latest=progress_;
        for(auto& w:waits_)if(w.second.until>latest)latest=w.second.until;
        return now>=latest && now-latest>=timeout_;
    }
    struct Wait{std::string reason;sc_core::sc_time until;};
    const std::map<std::string,Wait>& snapshot()const{return waits_;}
private:sc_core::sc_time timeout_,progress_;std::map<std::string,Wait> waits_;
};
}
