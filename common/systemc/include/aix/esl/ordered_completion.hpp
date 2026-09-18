#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
namespace aix::esl {
// Unique transaction tags and a separate stream ID; completion may be out of order,
// retirement preserves each stream's admission order. Credits last until retirement.
class OrderedCompletion {
public:
    struct Entry {uint64_t tag,stream;bool completed=false,success=false;};
    explicit OrderedCompletion(size_t capacity):capacity_(capacity){if(!capacity)throw std::invalid_argument("ROB capacity");}
    bool submit(uint64_t tag,uint64_t stream){
        for(const auto& e:entries_)if(e.tag==tag)throw std::logic_error("duplicate transaction tag");
        if(entries_.size()==capacity_)return false;
        entries_.push_back({tag,stream,false,false});return true;
    }
    void complete(uint64_t tag,bool success){
        for(auto& e:entries_)if(e.tag==tag){if(e.completed)throw std::logic_error("duplicate completion");e.completed=true;e.success=success;return;}
        throw std::logic_error("unknown completion");
    }
    std::optional<Entry> retire(uint64_t stream){
        auto it=std::find_if(entries_.begin(),entries_.end(),[stream](const Entry& e){return e.stream==stream;});
        if(it==entries_.end()||!it->completed)return std::nullopt;
        auto result=*it;entries_.erase(it);return result;
    }
    bool barrier_ready()const{return entries_.empty();}
    size_t outstanding()const{return entries_.size();}
private:size_t capacity_;std::deque<Entry> entries_;
};
}
