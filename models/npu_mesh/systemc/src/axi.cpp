#include <npu_mesh/axi.hpp>
#include <map>
#include <stdexcept>
#include <algorithm>
namespace aix::esl::npu_mesh {
struct AxiNiu::Impl {
    Model& bus;unsigned source,context,width,depth;
    struct Write {AxiAddress address;Request request;unsigned received=0;Status status=Status::ok;std::uint64_t handle=0;};
    struct Read {AxiAddress address;Request request;Status status=Status::ok;std::uint64_t handle=0;};
    std::deque<Write> writes;std::deque<Read> reads;std::deque<AxiB> bs;std::deque<AxiR> rs;
    unsigned pending_reads=0;
    Impl(Model& b,unsigned s,unsigned c,unsigned w,unsigned d):bus(b),source(s),context(c),width(w),depth(d){
        if(!w||(w&(w-1))||w>128||!d||s>=b.config().columns*b.config().rows||c>=64)throw std::invalid_argument("AXI NIU configuration");}
    Status prepare(const AxiAddress& a,Request& r,bool write){
        r.op=write?Op::write:Op::read;r.source=source;r.context=context;r.id=a.id;r.address=a.address;r.token=a.token;
        if(!a.beats||a.beats>256||!a.beat_bytes||(a.beat_bytes&(a.beat_bytes-1))||a.beat_bytes>width)return Status::unsupported;
        unsigned span=a.beats*a.beat_bytes-a.address%a.beat_bytes;r.length=span;
        if(a.address>UINT64_MAX-span||span>4096-a.address%4096||span>bus.config().max_transfer)return Status::unsupported;
        if(!a.incr||a.exclusive||a.device)return Status::unsupported;
        if(write){r.data.resize(span);r.enables.resize(span);}return write?Status::ok:bus.validate_request(r);
    }
    void tick(){
        // Responses retain capacity until consumed; read/write ordering are independent.
        for(auto it=writes.begin();it!=writes.end();){auto& t=*it;
            bool earlier=false;for(auto j=writes.begin();j!=it;++j)if(j->address.id==t.address.id)earlier=true;
            if(earlier||t.received<t.address.beats){++it;continue;}
            if(!t.handle&&t.status==Status::ok){auto s=bus.submit(t.request);if(s.status==Status::retry){++it;continue;}t.status=s.status;t.handle=s.handle;}
            if(t.handle){Completion c;if(!bus.take(t.handle,c)){++it;continue;}t.status=c.status;}
            bs.push_back({t.address.id,t.status});it=writes.erase(it);
        }
        for(auto it=reads.begin();it!=reads.end();){auto& t=*it;
            bool earlier=false;for(auto j=reads.begin();j!=it;++j)if(j->address.id==t.address.id)earlier=true;
            if(earlier){++it;continue;}
            if(!t.handle&&t.status==Status::ok){auto s=bus.submit(t.request);if(s.status==Status::retry){++it;continue;}t.status=s.status;t.handle=s.handle;}
            Completion c;if(t.handle){if(!bus.take(t.handle,c)){++it;continue;}t.status=c.status;}
            for(unsigned beat=0;beat<t.address.beats;++beat){AxiR result{t.address.id,t.status,beat+1==t.address.beats,std::vector<unsigned char>(width,0)};
                if(t.status==Status::ok){auto aligned=t.address.address-t.address.address%t.address.beat_bytes;
                    auto start=beat?aligned+beat*t.address.beat_bytes:t.address.address;auto end=aligned+(beat+1)*t.address.beat_bytes;
                    for(auto a=start;a<end;++a)result.data[a%width]=c.data.at(a-t.address.address);}
                rs.push_back(std::move(result));}
            it=reads.erase(it);
        }
    }
};
AxiNiu::AxiNiu(sc_core::sc_module_name n,Model& b,unsigned source,unsigned context,unsigned width,unsigned depth):sc_module(n),p(new Impl(b,source,context,width,depth)){SC_THREAD(run);}
AxiNiu::~AxiNiu()=default;
void AxiNiu::run(){while(true){wait(1,sc_core::SC_NS);p->tick();}}
bool AxiNiu::aw(const AxiAddress& a){if(p->writes.size()+p->bs.size()>=p->depth||!a.beats||a.beats>256)return false;
    Impl::Write t;t.address=a;t.status=p->prepare(a,t.request,true);p->writes.push_back(std::move(t));return true;}
bool AxiNiu::w(const AxiWriteBeat& w){
    auto it=std::find_if(p->writes.begin(),p->writes.end(),[](const auto& t){return t.received<t.address.beats;});
    if(it==p->writes.end())return false;auto& t=*it;auto beat=t.received;
    if(w.data.size()!=p->width||w.strobes.size()!=p->width||w.last!=(beat+1==t.address.beats))t.status=Status::unsupported;
    if(t.status==Status::ok){auto aligned=t.address.address-t.address.address%t.address.beat_bytes;
        auto start=beat?aligned+beat*t.address.beat_bytes:t.address.address;auto end=aligned+(beat+1)*t.address.beat_bytes;
        for(unsigned lane=0;lane<p->width;++lane)if(w.strobes[lane]){
            auto absolute=start-start%p->width+lane;
            if(absolute<start||absolute>=end){t.status=Status::unsupported;break;}}
        if(t.status==Status::ok)for(auto a=start;a<end;++a){t.request.data[a-t.address.address]=w.data[a%p->width];t.request.enables[a-t.address.address]=w.strobes[a%p->width];}
    }
    ++t.received;return true;
}
bool AxiNiu::ar(const AxiAddress& a){if(p->pending_reads>=p->depth||!a.beats||a.beats>256)return false;
    Impl::Read t;t.address=a;t.status=p->prepare(a,t.request,false);p->reads.push_back(std::move(t));++p->pending_reads;return true;}
bool AxiNiu::b(AxiB& b){if(p->bs.empty())return false;b=p->bs.front();p->bs.pop_front();return true;}
bool AxiNiu::r(AxiR& r){if(p->rs.empty())return false;r=p->rs.front();p->rs.pop_front();if(r.last)--p->pending_reads;return true;}
bool AxiNiu::idle()const{return p->writes.empty()&&p->reads.empty()&&p->bs.empty()&&p->rs.empty();}
}
