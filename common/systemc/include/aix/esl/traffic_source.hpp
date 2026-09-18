#pragma once
#include <aix/esl/deterministic_rng.hpp>
#include <aix/esl/transaction.hpp>
namespace aix::esl {
// Sequence generation is untimed. A SystemC initiator controls phase, acceptance,
// outstanding credits, completion and retry without regenerating rejected requests.
class TrafficSource {
public:
    enum class Pattern{sequential,stride,random,hotspot};
    TrafficSource(uint64_t base,uint64_t span,unsigned bytes,Pattern pattern,uint64_t stride,
                  unsigned write_percent,uint64_t seed,const std::string& stream)
        :base_(base),slots_(bytes?span/bytes:0),bytes_(bytes),pattern_(pattern),stride_(stride),writes_(write_percent),rng_(seed,stream){
        if(!bytes||!span||span%bytes||base%bytes||span-1>UINT64_MAX-base||write_percent>100||stride%bytes)
            throw std::invalid_argument("traffic geometry/rate");
    }
    Transaction next(){
        if(serial_==UINT64_MAX)throw std::overflow_error("traffic ID");
        uint64_t slot=cursor_;
        if(pattern_==Pattern::random)slot=rng_.uniform(slots_);
        else if(pattern_==Pattern::hotspot)slot=rng_.uniform(100)<80?0:rng_.uniform(slots_);
        Transaction t;t.address=base_+slot*bytes_;t.write=rng_.uniform(100)<writes_;t.metadata.id=serial_++;
        t.data.resize(bytes_);for(unsigned i=0;i<bytes_;++i)t.data[i]=static_cast<unsigned char>(t.address+i);
        uint64_t increment=pattern_==Pattern::stride?(stride_/bytes_)%slots_:1%slots_;
        cursor_=cursor_>=slots_-increment?cursor_-(slots_-increment):cursor_+increment;
        return t;
    }
private:uint64_t base_,slots_;unsigned bytes_;Pattern pattern_;uint64_t stride_;unsigned writes_;DeterministicRng rng_;uint64_t serial_=0,cursor_=0;
};
}
