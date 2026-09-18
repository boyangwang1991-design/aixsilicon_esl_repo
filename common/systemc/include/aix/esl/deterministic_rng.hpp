#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
namespace aix::esl {
// SplitMix64, named FNV-1a streams. Integer-only, independent of stdlib distributions.
class DeterministicRng {
public:
    DeterministicRng(uint64_t seed,const std::string& stream):state_(seed){
        uint64_t hash=14695981039346656037ULL;
        for(unsigned char c:stream){hash^=c;hash*=1099511628211ULL;}state_^=hash;
    }
    uint64_t next(){uint64_t z=(state_+=0x9e3779b97f4a7c15ULL);z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
        z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);}
    uint64_t uniform(uint64_t bound){
        if(!bound)throw std::invalid_argument("random bound");
        const uint64_t threshold=(-bound)%bound;
        for(;;){auto value=next();if(value>=threshold)return value%bound;}
    }
    uint64_t state()const{return state_;}
    void restore(uint64_t state){state_=state;}
private:uint64_t state_;
};
}
