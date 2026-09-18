#pragma once
#include <aix/esl/transaction.hpp>
#include <map>
#include <sstream>
#include <string>
namespace aix::esl {
// Independent byte oracle. Call write_committed in externally defined visibility
// order, and check_read at the model's read sampling point, not arbitrary return order.
class MemoryScoreboard {
public:
    explicit MemoryScoreboard(size_t bytes):expected_(bytes,0){if(!bytes)throw std::invalid_argument("scoreboard capacity");}
    void write_committed(const Transaction& transaction){
        range(transaction);if(!transaction.write)throw std::invalid_argument("scoreboard expected write");
        for(size_t i=0;i<transaction.data.size();++i)if(enabled(transaction,i))expected_[transaction.address+i]=transaction.data[i];
    }
    void check_read(const Transaction& transaction)const{
        range(transaction);if(transaction.write)throw std::invalid_argument("scoreboard expected read");
        for(size_t i=0;i<transaction.data.size();++i)if(enabled(transaction,i)&&expected_[transaction.address+i]!=transaction.data[i]){
            std::ostringstream message;message<<"read mismatch: id="<<transaction.metadata.id<<" source="<<transaction.metadata.source<<" address="<<transaction.address+i;
            throw std::runtime_error(message.str());}
    }
private:std::vector<unsigned char> expected_;
    static bool enabled(const Transaction& t,size_t i){return t.mask.empty()||t.mask[i%t.mask.size()]==0xff;}
    void range(const Transaction& t)const{t.validate();if(t.address>=expected_.size()||t.data.size()>expected_.size()-t.address)throw std::out_of_range("scoreboard address");}
};
class ConservationChecker {
public:
    explicit ConservationChecker(size_t capacity):capacity_(capacity){if(!capacity)throw std::invalid_argument("checker capacity");}
    void accept(uint64_t tag,uint64_t bytes){
        if(!bytes||live_.size()==capacity_||live_.count(tag)||bytes>UINT64_MAX-accepted_bytes_)
            throw std::logic_error("invalid/duplicate/over-capacity admission");
        live_.emplace(tag,bytes);accepted_bytes_+=bytes;++accepted_;
    }
    void complete(uint64_t tag,uint64_t bytes){
        auto it=live_.find(tag);if(it==live_.end()||it->second!=bytes)throw std::logic_error("unknown/duplicate/byte-loss completion");
        completed_bytes_+=bytes;++completed_;live_.erase(it);
    }
    void finish()const{if(!live_.empty()||accepted_!=completed_||accepted_bytes_!=completed_bytes_)throw std::logic_error("unfinished transaction conservation");}
private:size_t capacity_;std::map<uint64_t,uint64_t> live_;uint64_t accepted_=0,completed_=0,accepted_bytes_=0,completed_bytes_=0;
};
class BandwidthChecker {
public:
    BandwidthChecker(sc_core::sc_time period,uint64_t bytes):period_(period.value()),limit_(bytes){if(!period_||!limit_)throw std::invalid_argument("bandwidth geometry");}
    void transfer(uint64_t bytes){auto window=sc_core::sc_time_stamp().value()/period_;
        if(window!=window_){window_=window;used_=0;}
        if(bytes>limit_-used_)throw std::logic_error("bandwidth exceeded");used_+=bytes;
    }
private:uint64_t period_,limit_,window_=0,used_=0;
};
}
