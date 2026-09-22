#pragma once
#include <cstdint>
namespace aix::esl {
// Extended Hamming SECDED: 64 data bits + 7 Hamming bits + overall parity.
// Functional code only: scheduling/RMW/scrub use separate resources and policies.
struct Secded64 {
    struct Word{uint64_t data;uint8_t check;};
    enum class Status{clean,corrected,uncorrectable};
    struct Result{uint64_t data;Status status;};
    static bool parity_position(unsigned p){return !(p&(p-1));}
    static Word encode(uint64_t data){
        unsigned syndrome=0,parity=0,index=0;
        for(unsigned p=1;p<=71;++p)if(!parity_position(p)){
            if((data>>index)&1){syndrome^=p;parity^=1;}++index;}
        for(unsigned i=0;i<7;++i)parity^=(syndrome>>i)&1;
        return {data,static_cast<uint8_t>(syndrome|(parity<<7))};
    }
    static Result decode(Word word){
        auto expected=encode(word.data);unsigned syndrome=(expected.check^word.check)&127;
        unsigned parity=0;auto data=word.data;while(data){parity^=1;data&=data-1;}
        for(unsigned i=0;i<8;++i)parity^=(word.check>>i)&1;
        if(!syndrome)return {word.data,parity?Status::corrected:Status::clean};
        if(!parity||syndrome>71)return {word.data,Status::uncorrectable};
        if(!parity_position(syndrome)){
            unsigned index=0;for(unsigned p=1;p<syndrome;++p)if(!parity_position(p))++index;
            word.data^=uint64_t(1)<<index;
        }
        return {word.data,Status::corrected};
    }
};
}
