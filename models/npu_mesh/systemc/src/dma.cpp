#include <npu_mesh/model.hpp>
#include <map>
#include <list>
#include <stdexcept>
#include <algorithm>
namespace aix::esl::npu_mesh {
struct TensorDma::Impl {
    Model& bus;
    unsigned capacity;
    std::uint64_t next=1,source_bytes=0;
    struct Chunk {
        unsigned offset=0,bytes=0,next_destination=0;
        std::uint64_t read=0;
        bool loaded=false;
        std::vector<unsigned char> data;
        std::map<unsigned,std::uint64_t> writes;
    };
    struct Job {
        DmaDescriptor descriptor;
        DmaCompletion result;
        unsigned first=0,end=0,next_offset=0;
        std::uint64_t epoch=0;
        bool cancel=false;
        std::list<Chunk> chunks;
    };
    std::map<std::uint64_t,Job> jobs;
    std::map<std::uint64_t,DmaCompletion> done;
    Impl(Model& b,unsigned c):bus(b),capacity(c){if(!c)throw std::invalid_argument("DMA capacity zero");}
    void fail(Job& j,Status status,bool uncertain=false) {
        if(j.result.status==Status::ok)j.result.status=status;
        j.result.uncertain|=uncertain;
    }
    Request request(const Job& j,const Chunk& c,bool write,unsigned destination=0) {
        const auto& s=j.descriptor.segments[write?destination:j.first];
        Request r;r.op=write?Op::write:Op::read;r.source=j.descriptor.source;
        r.context=j.descriptor.context;r.id=j.descriptor.id;r.ordered=false;
        r.address=(write?s.destination:s.source)+c.offset;r.length=c.bytes;
        r.token=write?s.token:0;if(write)r.data=c.data;return r;
    }
    void group(Job& j) {
        j.end=j.first+1;
        auto& head=j.descriptor.segments[j.first];
        while(j.end<j.descriptor.segments.size()) {
            auto& next=j.descriptor.segments[j.end];
            if(next.source!=head.source||next.bytes!=head.bytes)break;
            ++j.end;
        }
        j.next_offset=0;
    }
    void tick() {
        std::vector<std::uint64_t> finished;
        for(auto& item:jobs) {
            auto& j=item.second;
            if(j.cancel||j.epoch!=bus.epoch())fail(j,Status::aborted,j.epoch!=bus.epoch());
            // Drain ALL accepted children before terminal completion or releasing tokens.
            for(auto& chunk:j.chunks) {
                if(chunk.read) {
                    Completion c;
                    if(bus.take(chunk.read,c)) {
                        chunk.read=0;
                        if(c.status!=Status::ok)fail(j,c.status,c.uncertain);
                        else {chunk.loaded=true;chunk.data=std::move(c.data);source_bytes+=chunk.data.size();}
                    }
                }
                for(auto it=chunk.writes.begin();it!=chunk.writes.end();) {
                    Completion c;
                    if(!bus.take(it->second,c)){++it;continue;}
                    unsigned destination=it->first;
                    j.result.bytes+=c.bytes;j.result.bytes_per_destination[destination]+=c.bytes;
                    j.result.completed[destination]=j.result.bytes_per_destination[destination]==j.descriptor.segments[destination].bytes;
                    if(c.status!=Status::ok)fail(j,c.status,c.uncertain);
                    it=chunk.writes.erase(it);
                }
            }
            if(j.result.status!=Status::ok) {
                bool pending=false;
                for(auto& c:j.chunks)pending|=c.read||!c.writes.empty();
                if(!pending)finished.push_back(item.first);
                continue;
            }
            for(auto it=j.chunks.begin();it!=j.chunks.end();) {
                auto& c=*it;
                if(c.loaded&&c.next_destination==j.end&&c.writes.empty())it=j.chunks.erase(it);
                else ++it;
            }
            if(j.first==j.descriptor.segments.size()){finished.push_back(item.first);continue;}
            if(j.chunks.empty()&&j.next_offset==j.descriptor.segments[j.first].bytes) {
                for(unsigned d=j.first;d<j.end;++d)j.result.completed[d]=true;
                j.first=j.end;
                if(j.first==j.descriptor.segments.size()){finished.push_back(item.first);continue;}
                group(j);
            }
            auto& source=j.descriptor.segments[j.first];
            // Bounded data buffers: at most dma_window chunks, each <= max_transfer.
            if(j.chunks.size()<bus.config().dma_window&&j.next_offset<source.bytes) {
                Chunk c;c.offset=j.next_offset;c.next_destination=j.first;
                c.bytes=std::min({bus.config().max_transfer,source.bytes-c.offset,unsigned(4096-(source.source+c.offset)%4096)});
                for(unsigned d=j.first;d<j.end;++d)
                    c.bytes=std::min(c.bytes,unsigned(4096-(j.descriptor.segments[d].destination+c.offset)%4096));
                auto s=bus.submit(request(j,c,false));
                if(s.status==Status::ok){c.read=s.handle;j.next_offset+=c.bytes;j.chunks.push_back(std::move(c));}
                else if(s.status!=Status::retry)fail(j,s.status);
            }
            // For each chunk, fan out before discarding its source data. Large multicast reads once.
            for(auto& c:j.chunks) {
                if(!c.loaded||c.next_destination==j.end||j.result.status!=Status::ok)continue;
                auto s=bus.submit(request(j,c,true,c.next_destination));
                if(s.status==Status::ok)c.writes.emplace(c.next_destination++,s.handle);
                else if(s.status!=Status::retry)fail(j,s.status);
            }
        }
        for(auto id:finished) {
            auto& j=jobs.at(id);
            for(auto& s:j.descriptor.segments)if(s.token)bus.unpin(s.token);
            done[id]=j.result;jobs.erase(id);
        }
    }
};
TensorDma::TensorDma(sc_core::sc_module_name n,Model& b,unsigned capacity):sc_module(n),p(new Impl(b,capacity)){SC_THREAD(run);}
TensorDma::~TensorDma()=default;
void TensorDma::run(){while(true){wait(1,sc_core::SC_NS);p->tick();}}
Submission TensorDma::submit(const DmaDescriptor& d) {
    if(d.source>=p->bus.config().columns*p->bus.config().rows||d.context>=64)return{Status::denied,0};
    if(p->jobs.size()+p->done.size()>=p->capacity)return{Status::retry,0};
    if(d.segments.size()>4096)return{Status::unsupported,0};
    std::uint64_t total=0;
    for(unsigned index=0;index<d.segments.size();++index) {
        const auto& s=d.segments[index];total+=s.bytes;
        if(total>UINT32_MAX)return{Status::unsupported,0};
        if(s.source>UINT64_MAX-s.bytes||s.destination>UINT64_MAX-s.bytes)return{Status::decode,0};
        for(auto& t:d.segments)
            if(s.bytes&&t.bytes&&s.destination<t.source+t.bytes&&t.source<s.destination+s.bytes)return{Status::unsupported,0};
        // Destination aliasing would make concurrent children ambiguous. Reject before side effects.
        for(unsigned prior=0;prior<index;++prior) {
            auto& t=d.segments[prior];
            if(s.bytes&&t.bytes&&s.destination<t.destination+t.bytes&&t.destination<s.destination+s.bytes)return{Status::unsupported,0};
        }
        for(unsigned offset=0;offset<s.bytes;) {
            unsigned count=std::min(p->bus.config().max_transfer,s.bytes-offset);
            Request r;r.source=d.source;r.context=d.context;r.id=d.id;r.address=s.source+offset;r.length=count;
            auto status=p->bus.validate_request(r);if(status!=Status::ok)return{status,0};
            r.op=Op::write;r.address=s.destination+offset;r.token=s.token;r.data.resize(count);
            status=p->bus.validate_request(r);if(status!=Status::ok)return{status,0};offset+=count;
        }
    }
    auto h=p->next++;Impl::Job job;job.descriptor=d;job.epoch=p->bus.epoch();
    job.result={h,Status::ok,0,std::vector<bool>(d.segments.size(),false),std::vector<unsigned>(d.segments.size(),0),false};
    if(!d.segments.empty())p->group(job);
    for(auto& s:d.segments)if(s.token)p->bus.pin(s.token);
    p->jobs.emplace(h,std::move(job));return{Status::ok,h};
}
bool TensorDma::take(std::uint64_t h,DmaCompletion& c){auto it=p->done.find(h);if(it==p->done.end())return false;c=it->second;p->done.erase(it);return true;}
void TensorDma::cancel(std::uint64_t h){auto i=p->jobs.find(h);if(i!=p->jobs.end())i->second.cancel=true;}
bool TensorDma::idle()const{return p->jobs.empty()&&p->done.empty();}
std::uint64_t TensorDma::source_bytes()const{return p->source_bytes;}
DmaDescriptor TensorDma::strided(unsigned source,unsigned context,unsigned id,std::uint64_t from,std::uint64_t to,unsigned rows,unsigned bytes,unsigned ss,unsigned ds) {
    if(rows>4096||ss<bytes||ds<bytes||(rows&&(from>UINT64_MAX-std::uint64_t(rows-1)*ss||to>UINT64_MAX-std::uint64_t(rows-1)*ds)))throw std::invalid_argument("invalid strided descriptor");
    DmaDescriptor d;d.source=source;d.context=context;d.id=id;
    for(unsigned row=0;row<rows;++row)d.segments.push_back({from+std::uint64_t(row)*ss,to+std::uint64_t(row)*ds,bytes,0});
    return d;
}
bool SyncEvents::arrive(unsigned context,std::uint64_t epoch,std::uint64_t event,unsigned participant,Status status){
    if(epoch!=epoch_||participant>=64||status==Status::retry)return false;
    for(auto& e:entries)if(e.context==context&&e.epoch==epoch&&e.event==event){
        auto bit=std::uint64_t(1)<<participant;if(e.mask&bit)return false;e.mask|=bit;if(e.status==Status::ok)e.status=status;return true;}
    if(entries.size()>=4096)return false;entries.push_back({context,epoch,event,std::uint64_t(1)<<participant,status});return true;
}
Status SyncEvents::query(unsigned context,std::uint64_t epoch,std::uint64_t event,std::uint64_t expected)const{
    if(epoch!=epoch_)return Status::stale;for(auto& e:entries)if(e.context==context&&e.epoch==epoch&&e.event==event){if(e.status!=Status::ok)return e.status;return (e.mask&expected)==expected?Status::ok:Status::retry;}return Status::retry;
}
void SyncEvents::reset(std::uint64_t epoch){if(epoch<epoch_)throw std::invalid_argument("event epoch regression");epoch_=epoch;entries.clear();}
}
