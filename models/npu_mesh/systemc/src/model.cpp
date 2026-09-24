#include <npu_mesh/model.hpp>
#include "network.hpp"
#include "target.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
namespace aix::esl::npu_mesh {
const char* status_name(Status s) {
    static const char* names[]={"OK","RETRY","DECODE","DENIED","UNSUPPORTED","TARGET","DATA_CORRUPT","ABORTED","STALE_EPOCH","TIMEOUT_UNCERTAIN"};
    return names[unsigned(s)];
}
void Config::validate() const {
    if(!columns||!rows||columns>16||rows>16||!depth||depth>256||!vcs_per_vn||vcs_per_vn>4||
       !link_latency||!credit_latency||!router_latency||!packet_bytes||!outstanding||!return_bytes||
       !command_depth||!command_latency||!max_transfer||!timeout_cycles||endpoints.empty()||outstanding>65535||return_bytes>1073741824||
       !fragment_window||fragment_window>64||!dma_window||dma_window>64)
        throw std::invalid_argument("zero/unsupported mesh resource configuration");
    if((link_bytes!=16&&link_bytes!=32&&link_bytes!=64&&link_bytes!=128)||
       (control_bytes!=4&&control_bytes!=8&&control_bytes!=16&&control_bytes!=32)||
       packet_bytes>max_transfer||max_transfer>return_bytes||max_transfer>1048576)
        throw std::invalid_argument("width/packet/return reservation configuration");
    if(shape_context_min>64||!shape_burst_bytes||(shape_bytes_per_cycle&&shape_burst_bytes<packet_bytes))
        throw std::invalid_argument("invalid NIU shaping policy");
    for(unsigned i=0;i<endpoints.size();++i) {
        const auto& e=endpoints[i];
        if(e.router>=columns*rows||!e.size||e.size>67108864||e.base>UINT64_MAX-e.size||!e.bytes_per_cycle||!e.slots)
            throw std::invalid_argument("invalid endpoint");
        if(e.backend!="ddr"&&e.backend!="sram")throw std::invalid_argument("unknown memory backend");
        if(e.priority_context>64||!e.age_cycles||e.issue_limit>65535||
           (e.backend!="sram"&&(e.priority_context!=64||e.issue_limit)))
            throw std::invalid_argument("invalid SRAM entrance QoS policy");
        if(e.backend=="sram") {
            if(e.base%4096||e.size%128||e.sram.mapping=="region"||e.sram.scrub_interval)
                throw std::invalid_argument("SRAM adapter requires 4KB base, 128B capacity, full map and no scrub");
            auto s=e.sram;s.capacity=e.size;s.full_data=1;s.validate();
        }
        for(unsigned j=0;j<i;++j)
            if(e.base<endpoints[j].base+endpoints[j].size&&endpoints[j].base<e.base+e.size)
                throw std::invalid_argument("overlapping endpoint map");
    }
}
struct Model::Impl {
    Config cfg;
    Metrics stats;
    Network net;
    std::uint64_t next=1,epoch=0,token_next=1;
    bool blocked=false,resetting=false;
    struct Txn {
        Request request;
        Completion result;
        unsigned endpoint=0, issued=0;
        std::map<unsigned,unsigned> pending; // Offset -> length; reserves fragment ACK metadata.
        Status error=Status::ok;
        unsigned error_offset=~0u;
        std::uint64_t watermark=0;
    };
    struct Reservation {
        unsigned context;
        std::uint64_t address;
        unsigned length;
        bool poisoned=false;
        unsigned references=0;
    };
    struct Target {
        MemoryTarget backend;
        unsigned reserved=0;
        bool stalled=false,corrupt=false;
        std::deque<std::shared_ptr<Packet>> responses;
        Target(const Endpoint& e,unsigned index):backend(e,index){}
    };
    struct Bucket {std::uint64_t tokens, cycle;};
    std::map<std::pair<unsigned,unsigned>,Bucket> buckets;
    std::vector<std::unique_ptr<Target>> targets;
    std::map<std::uint64_t,Txn> active;
    std::map<std::uint64_t,Completion> done;
    std::map<std::uint64_t,Reservation> reservations;
    std::map<std::pair<unsigned,unsigned>,bool> failed_domain;
    std::vector<std::deque<std::pair<std::uint64_t,std::uint64_t>>> commands;
    std::vector<Trace> traces;
    sc_core::sc_event completion;
    explicit Impl(Config c):cfg(c),net(c,stats),commands(c.columns*c.rows) {
        cfg.validate();
        for(unsigned i=0;i<c.endpoints.size();++i)targets.emplace_back(new Target(c.endpoints[i],i));
        net.admit=[this](const Packet& p) {
            if(p.epoch!=epoch||!active.count(p.handle))return true;
            if(p.vn==0||p.vn==1) {
                auto& t=*targets[p.endpoint];
                if(t.reserved>=cfg.endpoints[p.endpoint].slots)return false;
                ++t.reserved;p.endpoint_reserved=true;
            }
            return true;
        };
        net.receive=[this](std::shared_ptr<Packet> p){receive(p);};
    }
    void log(const Txn& t,const char* module,const char* event,unsigned bytes=0) {
        if(cfg.trace)traces.push_back({stats.cycles,t.result.handle,module,event,t.request.source,t.request.context,bytes});
    }
    int endpoint(std::uint64_t a,unsigned n)const {
        if(a>UINT64_MAX-n)return -1;
        for(unsigned i=0;i<cfg.endpoints.size();++i) {
            const auto& e=cfg.endpoints[i];
            if(a>=e.base&&a-e.base<=e.size&&n<=e.size-(a-e.base))return int(i);
        }
        return -1;
    }
    Status valid(const Request& r)const {
        if(r.source>=cfg.columns*cfg.rows||r.context>=64)return Status::denied;
        if(r.op==Op::fence)return r.length?Status::unsupported:Status::ok;
        if(!r.length||r.length>cfg.max_transfer)return Status::unsupported;
        int e=endpoint(r.address,r.length);if(e<0)return Status::decode;
        if(!(cfg.endpoints[e].contexts&(std::uint64_t(1)<<r.context)))return Status::denied;
        if(r.op==Op::write) {
            if(r.data.size()!=r.length||(!r.enables.empty()&&r.enables.size()!=r.length))return Status::unsupported;
            if(cfg.endpoints[e].tile||r.token) {
                auto i=reservations.find(r.token);
                if(i==reservations.end()||i->second.context!=r.context||i->second.poisoned)return Status::denied;
                const auto& b=i->second;
                if(r.address<b.address||r.address-b.address>b.length||r.length>b.length-(r.address-b.address))return Status::denied;
            }
        }
        return Status::ok;
    }
    void stop(Txn& t,Status status,unsigned offset,bool uncertain=false) {
        if(t.error==Status::ok||offset<t.error_offset){t.error=status;t.error_offset=offset;}
        t.result.uncertain|=uncertain;
        auto token=reservations.find(t.request.token);
        if(token!=reservations.end())token->second.poisoned=true;
    }
    void finish(std::uint64_t h,Status s,bool uncertain=false) {
        auto it=active.find(h);if(it==active.end())return;
        auto& t=it->second;
        if(!t.pending.empty())throw std::logic_error("completion before fragment drain");
        t.result.status=s;t.result.uncertain|=uncertain;t.result.done=stats.cycles;
        if(s!=Status::ok) {
            if(t.result.epoch==epoch)failed_domain[{t.request.source,t.request.context}]=true;
            auto token=reservations.find(t.request.token);
            if(token!=reservations.end())token->second.poisoned=true;
            if(t.request.op==Op::read)std::fill(t.result.data.begin(),t.result.data.end(),0);
        }
        if(t.request.token&&t.request.op==Op::write) {
            auto r=reservations.find(t.request.token);
            if(r!=reservations.end()&&r->second.references)--r->second.references;
        }
        std::sort(t.result.completed_ranges.begin(),t.result.completed_ranges.end());
        log(t,"command_event","TASK_DONE");done[h]=t.result;++stats.completed;
        active.erase(it);completion.notify(sc_core::SC_ZERO_TIME);
    }
    void retire(std::shared_ptr<Packet> p) {
        auto it=active.find(p->handle);if(it==active.end())return;
        auto& t=it->second;
        auto pending=t.pending.find(p->offset);
        if(pending==t.pending.end()||pending->second!=p->bytes)throw std::logic_error("duplicate/mismatched fragment response");
        if(p->status!=Status::ok&&t.error==Status::ok)stop(t,p->status,p->offset);
        if(p->status==Status::ok&&p->vn==2)
            std::copy(p->data.begin(),p->data.end(),t.result.data.begin()+p->offset);
        t.pending.erase(pending);
        if(t.pending.empty()&&(t.error!=Status::ok||t.issued==t.request.length))finish(p->handle,t.error);
    }
    void receive(std::shared_ptr<Packet> p) {
        auto it=active.find(p->handle);
        if(p->epoch!=epoch||it==active.end()) {
            if(p->epoch==epoch&&p->endpoint_reserved&&(p->vn==0||p->vn==1))--targets[p->endpoint]->reserved;
            ++stats.late_packets;return;
        }
        auto& t=it->second;
        if(p->vn==0||p->vn==1) {
            auto& target=*targets[p->endpoint];
            if(target.corrupt){p->status=Status::corrupt;target.corrupt=false;}
            target.backend.accept(p,stats.cycles);log(t,"memory_niu","TRANSPORT_ACK");
        } else retire(p);
    }
    void tick() {
        ++stats.cycles;
        // Stop injection at timeout, but never release storage ownership before native writes drain.
        for(auto& item:active) {
            auto& t=item.second;
            if(t.error==Status::ok&&stats.cycles-t.result.accepted>=cfg.timeout_cycles)
                stop(t,Status::timeout,0,true);
        }
        for(unsigned i=0;i<targets.size();++i) {
            auto& target=*targets[i];
            target.backend.tick(stats.cycles,target.stalled,[this](std::uint64_t h) {
                auto it=active.find(h);return it==active.end()||it->second.error!=Status::ok;
            });
            std::shared_ptr<Packet> response;
            while(target.backend.pop(response)) {
                auto it=active.find(response->handle);
                if(it==active.end())throw std::logic_error("backend transaction disappeared before drain");
                auto& t=it->second;
                stats.target_bytes+=response->bytes;
                if(response->status==Status::target&&t.request.op==Op::write)t.result.uncertain=true;
                if(response->status==Status::ok) {
                    stats.useful_bytes+=response->bytes;t.result.visible=stats.cycles;
                    t.result.bytes+=response->bytes;
                    t.result.completed_ranges.push_back({response->offset,response->bytes});
                    log(t,"memory_niu","TARGET_VISIBLE");
                } else if(t.error==Status::ok)stop(t,response->status,response->offset);
                if(resetting) {--target.reserved;retire(response);}
                else target.responses.push_back(response);
            }
            if(!target.responses.empty()&&net.inject(target.responses.front())) {
                target.responses.pop_front();--target.reserved;
            }
        }
        std::vector<std::pair<std::uint64_t,Status>> terminal;
        for(auto& pair:active) {
            auto& t=pair.second;auto& r=t.request;
            if(t.error!=Status::ok) {
                if(t.pending.empty())terminal.push_back({pair.first,t.error});
                continue;
            }
            if(r.op==Op::fence) {
                bool pending=false;
                for(auto& other:active)
                    if(other.first<=t.watermark&&other.second.request.source==r.source&&other.second.request.context==r.context)pending=true;
                if(!pending)terminal.push_back({pair.first,failed_domain[{r.source,r.context}]?Status::target:Status::ok});
                continue;
            }
            if(t.pending.size()>=cfg.fragment_window||t.issued==r.length)continue;
            bool earlier=false;
            for(auto& prev:active)
                if(prev.first<pair.first&&prev.second.request.source==r.source&&prev.second.request.context==r.context&&
                   ((r.ordered&&prev.second.request.ordered&&prev.second.request.op==r.op&&prev.second.request.id==r.id)||prev.second.request.op==Op::fence))earlier=true;
            if(earlier)continue;
            unsigned n=std::min({cfg.packet_bytes,r.length-t.issued,unsigned(4096-((r.address+t.issued)%4096))});
            Bucket* bucket=nullptr;
            if(cfg.shape_bytes_per_cycle&&r.context>=cfg.shape_context_min) {
                auto key=std::make_pair(r.source,r.context);
                auto entry=buckets.emplace(key,Bucket{cfg.shape_burst_bytes,stats.cycles});
                bucket=&entry.first->second;
                auto elapsed=stats.cycles-bucket->cycle;
                auto fill=std::uint64_t(cfg.shape_burst_bytes)/cfg.shape_bytes_per_cycle+1;
                bucket->tokens=std::min(std::uint64_t(cfg.shape_burst_bytes),bucket->tokens+std::min(elapsed,fill)*cfg.shape_bytes_per_cycle);
                bucket->cycle=stats.cycles;
                if(bucket->tokens<n){++stats.shape_stalls;continue;}
            }
            auto p=std::make_shared<Packet>();
            p->context=r.context;p->handle=pair.first;p->epoch=epoch;p->source=r.source;p->destination=cfg.endpoints[t.endpoint].router;
            p->endpoint=t.endpoint;p->vn=r.op==Op::read?0:1;p->bytes=n;p->offset=t.issued;
            p->address=r.address+t.issued-cfg.endpoints[t.endpoint].base;
            if(r.op==Op::write) {
                p->data.assign(r.data.begin()+t.issued,r.data.begin()+t.issued+n);
                if(!r.enables.empty()) {
                    p->enables.assign(r.enables.begin()+t.issued,r.enables.begin()+t.issued+n);
                    for(auto& b:p->enables)b=b?255:0;
                }
            }
            if(net.inject(p)) {
                if(bucket){bucket->tokens-=n;stats.shaped_bytes+=n;}
                t.pending.emplace(t.issued,n);t.issued+=n;
                stats.fragment_peak=std::max(stats.fragment_peak,unsigned(t.pending.size()));
                log(t,"axi_niu","INJECTED_FRAGMENT",n);
            }
        }
        for(auto item:terminal)finish(item.first,item.second);
        net.tick(stats.cycles);
    }
};
Model::Model(sc_core::sc_module_name name,Config c):sc_module(name){
    for(auto& e:c.endpoints)if(e.backend=="sram"){e.sram.capacity=e.size;e.sram.full_data=1;}
    c.validate();p.reset(new Impl(c));SC_THREAD(run);
}
Model::~Model()=default;
void Model::run(){while(true){wait(1,sc_core::SC_NS);p->tick();}}
Status Model::validate_request(const Request& r)const{return p->valid(r);}
Submission Model::submit(const Request& r){
    auto s=p->valid(r);if(s!=Status::ok)return{s,0};
    unsigned count=0,returned=0;for(auto& x:p->active)if(x.second.request.source==r.source){++count;if(x.second.request.op==Op::read)returned+=x.second.request.length;}
    for(auto& x:p->done)if(x.second.source==r.source){++count;returned+=x.second.data.size();}
    if(p->blocked||count>=p->cfg.outstanding||(r.op==Op::read&&r.length>p->cfg.return_bytes-returned)){++p->stats.admission_stalls;return{Status::retry,0};}
    auto h=p->next++;Impl::Txn t;t.request=r;t.result.handle=h;t.result.epoch=p->epoch;t.result.accepted=p->stats.cycles;
    t.result.source=r.source;t.result.context=r.context;t.result.id=r.id;t.watermark=h-1;
    if(r.op!=Op::fence)t.endpoint=p->endpoint(r.address,r.length);
    if(r.op==Op::read)t.result.data.resize(r.length);
    if(r.op==Op::read)p->stats.return_reserved_peak=std::max(p->stats.return_reserved_peak,returned+r.length);
    if(r.op==Op::write&&r.token)++p->reservations.at(r.token).references;
    p->log(t,"axi_niu","ACCEPTED");p->active.emplace(h,std::move(t));++p->stats.accepted;return{Status::ok,h};
}
bool Model::take(std::uint64_t h,Completion& c){auto it=p->done.find(h);if(it==p->done.end())return false;c=it->second;p->done.erase(it);return true;}
const sc_core::sc_event& Model::completion_event()const{return p->completion;}
bool Model::idle()const{if(!p->active.empty()||!p->done.empty()||!p->net.idle())return false;for(auto& q:p->commands)if(!q.empty())return false;for(auto& t:p->targets)if(t->reserved||!t->backend.idle())return false;return true;}
void Model::drain(){p->blocked=true;}
bool Model::resume(){if(!idle())return false;p->blocked=false;p->resetting=false;return true;}
void Model::reset(){
    p->blocked=true;p->resetting=true;p->buckets.clear();
    for(auto& t:p->active){p->stop(t.second,Status::aborted,0,true);t.second.error=Status::aborted;t.second.pending.clear();}
    if(p->epoch==UINT64_MAX)throw std::overflow_error("epoch exhausted");++p->epoch;p->net.reset();
    for(auto& t:p->targets){
        auto native=t->backend.reset_pending();t->responses.clear();t->reserved=native.size();t->stalled=false;t->corrupt=false;
        for(auto& packet:native)p->active.at(packet->handle).pending.emplace(packet->offset,packet->bytes);
    }
    std::vector<std::uint64_t> ready;for(auto& t:p->active)if(t.second.pending.empty())ready.push_back(t.first);
    for(auto h:ready)p->finish(h,Status::aborted,true);
    for(auto& q:p->commands)q.clear();p->reservations.clear();p->failed_domain.clear();
}
bool Model::reconfigure_map(const std::vector<Endpoint>& e){
    if(!idle()||!p->blocked||e.size()!=p->cfg.endpoints.size())return false;auto c=p->cfg;c.endpoints=e;c.validate();
    for(unsigned i=0;i<e.size();++i)if(!p->targets[i]->backend.same_service(e[i]))return false;
    for(auto& r:p->reservations)if(r.second.references)return false;
    p->reservations.clear();p->cfg.endpoints=e;return true;
}
std::uint64_t Model::reserve(unsigned context,std::uint64_t address,unsigned length){
    int i=p->endpoint(address,length);if(i<0||!length||context>=64||!(p->cfg.endpoints[i].contexts&(std::uint64_t(1)<<context)))return 0;
    for(auto& x:p->reservations)if(address<x.second.address+x.second.length&&x.second.address<address+length)return 0;
    auto token=p->token_next++;p->reservations.emplace(token,Impl::Reservation{context,address,length,false,0});return token;
}
bool Model::release(std::uint64_t token){auto i=p->reservations.find(token);if(i==p->reservations.end()||i->second.references)return false;p->reservations.erase(i);return true;}
bool Model::pin(std::uint64_t token){auto i=p->reservations.find(token);if(i==p->reservations.end()||i->second.poisoned)return false;++i->second.references;return true;}
void Model::unpin(std::uint64_t token){auto i=p->reservations.find(token);if(i!=p->reservations.end()&&i->second.references)--i->second.references;}
bool Model::poisoned(std::uint64_t token)const{auto i=p->reservations.find(token);return i==p->reservations.end()||i->second.poisoned;}
void Model::stall_target(unsigned i,bool value){p->targets.at(i)->stalled=value;}
void Model::corrupt_next(unsigned i){p->targets.at(i)->corrupt=true;}
void Model::initialize(std::uint64_t a,const std::vector<unsigned char>& bytes){if(!idle())throw std::logic_error("initialize requires idle");int e=p->endpoint(a,bytes.size());if(e<0)throw std::invalid_argument("initialize address");p->targets[e]->backend.initialize(a-p->cfg.endpoints[e].base,bytes);}
std::vector<unsigned char> Model::inspect(std::uint64_t a,unsigned n)const{if(!idle())throw std::logic_error("inspect requires idle");int e=p->endpoint(a,n);if(e<0)throw std::invalid_argument("inspect address");return p->targets[e]->backend.inspect(a-p->cfg.endpoints[e].base,n);}
bool Model::command(unsigned tile,std::uint64_t value){auto& q=p->commands.at(tile);if(p->blocked||q.size()>=p->cfg.command_depth)return false;q.push_back({p->stats.cycles+p->cfg.command_latency,value});return true;}
bool Model::take_command(unsigned tile,std::uint64_t& value){auto& q=p->commands.at(tile);if(q.empty()||q.front().first>p->stats.cycles)return false;value=q.front().second;q.pop_front();return true;}
const Metrics& Model::metrics()const{return p->stats;}
const std::vector<PortMetrics>& Model::ports()const{return p->net.ports;}
const std::vector<Trace>& Model::trace()const{return p->traces;}
const Config& Model::config()const{return p->cfg;}
std::uint64_t Model::epoch()const{return p->epoch;}
void Model::target_report(unsigned e,std::ostream& out)const{p->targets.at(e)->backend.report(out);}
}
