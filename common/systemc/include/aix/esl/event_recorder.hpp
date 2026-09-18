#pragma once
#include <systemc>
#include <cstdint>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace aix::esl {
class EventRecorder {
public:
    enum class Mode{off,counters,trace};
    struct Event{uint64_t tick,id,parent;std::string source,phase,resource;uint64_t value;};
    explicit EventRecorder(Mode mode=Mode::counters,size_t limit=100000):mode_(mode),limit_(limit){}
    void emit(uint64_t id,uint64_t parent,const std::string& source,const std::string& phase,const std::string& resource,uint64_t value=0){
        if(mode_==Mode::off)return;
        if(!token(source)||!token(phase)||!token(resource))throw std::invalid_argument("event token");
        ++counts_[phase];
        if(mode_==Mode::trace){if(events_.size()<limit_)events_.push_back({sc_core::sc_time_stamp().value(),id,parent,source,phase,resource,value});else ++dropped_;}
    }
    void write(std::ostream& out)const{
        out<<"# aix-esl-events-v1,tick_seconds="<<sc_core::sc_get_time_resolution().to_seconds()<<",dropped="<<dropped_<<"\n";
        out<<"tick,id,parent,source,phase,resource,value\n";
        for(auto& e:events_)out<<e.tick<<','<<e.id<<','<<e.parent<<','<<e.source<<','<<e.phase<<','<<e.resource<<','<<e.value<<'\n';
        if(!out)throw std::runtime_error("event output failed");
    }
    const std::map<std::string,uint64_t>& counts()const{return counts_;}
    uint64_t dropped()const{return dropped_;}
private:
    static bool token(const std::string& s){return !s.empty()&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")==std::string::npos;}
    Mode mode_;size_t limit_;std::vector<Event> events_;std::map<std::string,uint64_t> counts_;uint64_t dropped_=0;
};
}
