#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
namespace aix::esl {
// Descriptor masks are supplied by the register SSOT generator. No address table here.
class RegisterBank {
public:
    struct Descriptor {uint32_t reset=0,rw=0,w1c=0,readable=0xffffffffu;};
    using Callback=std::function<void(uint32_t,uint32_t)>;
    void define(uint64_t offset,Descriptor descriptor,Callback on_write={}){
        if(offset%4 || descriptor.rw&descriptor.w1c || entries_.count(offset))
            throw std::invalid_argument("register alignment/overlapping access/duplicate");
        entries_.emplace(offset,Entry{descriptor,descriptor.reset,std::move(on_write)});
    }
    uint32_t read(uint64_t offset)const {const auto& e=entries_.at(offset);return e.value&e.descriptor.readable;}
    void write(uint64_t offset,uint32_t value,uint8_t byte_enable=15){
        if(byte_enable&0xf0)throw std::invalid_argument("register byte enable");
        auto& e=entries_.at(offset);uint32_t mask=0;
        for(unsigned i=0;i<4;++i)if(byte_enable&(1u<<i))mask|=0xffu<<(8*i);
        const auto before=e.value,rw=e.descriptor.rw&mask;
        e.value=((before&~rw)|(value&rw))&~(value&e.descriptor.w1c&mask);
        // Callback sees committed value; a throwing callback does not undo the bus write.
        if(e.on_write)e.on_write(before,e.value);
    }
    void update(uint64_t offset,uint32_t set,uint32_t clear=0){auto& e=entries_.at(offset);e.value=(e.value&~clear)|set;}
    void reset(){for(auto& item:entries_)item.second.value=item.second.descriptor.reset;}
    std::map<uint64_t,uint32_t> dump()const{std::map<uint64_t,uint32_t> result;for(const auto& p:entries_)result.emplace(p.first,p.second.value);return result;}
private:
    struct Entry{Descriptor descriptor;uint32_t value;Callback on_write;};std::map<uint64_t,Entry> entries_;
};
class InterruptState {
public:
    explicit InterruptState(unsigned threshold=1):threshold_(threshold){if(!threshold||threshold>32)throw std::invalid_argument("IRQ threshold");}
    void raise(uint32_t bits){pending_|=bits;}
    void clear(uint32_t bits){pending_&=~bits;}
    void mask(uint32_t enabled){enabled_=enabled;}
    bool asserted()const{auto bits=pending_&enabled_;unsigned n=0;while(bits){bits&=bits-1;++n;}return n>=threshold_;}
    uint32_t pending()const{return pending_;}
    void reset(){pending_=0;enabled_=0;}
private:uint32_t pending_=0,enabled_=0;unsigned threshold_;
};
}
