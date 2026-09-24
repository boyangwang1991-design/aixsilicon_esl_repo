#include "npu_sram_controller/model.hpp"
#include "aix/esl/byte_store.hpp"
#include "fabric.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
namespace aix::esl::npu_sram_controller {
namespace {
struct Transaction;
struct Beat {std::weak_ptr<Transaction> tx;unsigned index=0;bool w_ready=false,generated=false,done=false,error=false;
    unsigned pending=0,unsent=0;std::vector<uint8_t> data,mask;};
struct Transaction {Request r;uint64_t token=0,accepted=0;std::vector<std::shared_ptr<Beat>> beats;unsigned next_w=0,next_generate=0,next_response=0;uint64_t bytes=0;bool error=false;};
struct Fragment {std::shared_ptr<Beat> beat;Slice slice;unsigned port=0;uint64_t born=0,ready=0;
    bool write=false,rmw=false,locked=false,scrub=false,error=false,slot=false,repair=false;unsigned stage=0,codewords=0;
    // stage: 0 prepare, 1 ready bank, 2 bank flight, 3 ECC, 4 return, 5 done
    std::vector<unsigned> touched;std::vector<uint8_t> old;};
struct WBeat{std::vector<uint8_t> data,mask;bool last;};
struct EccWork {std::shared_ptr<Fragment> f;unsigned remaining;bool encode;std::function<void()> done;};
struct Event{uint64_t due;std::function<void()> fn;};
}
struct Model::Impl {
    Config c;Mapper mapper;detail::Fabric fabric;std::unique_ptr<aix::esl::ByteStore> storage;Metrics m;
    uint64_t now=0,next_token=1,trace_count=0;bool scrubbing=true;
    std::function<void(const std::string&)> observer;
    std::map<uint64_t,std::shared_ptr<Transaction>> txs;
    std::vector<std::deque<uint64_t>> reads,writes;
    std::vector<std::deque<WBeat>> wfifo;
    std::vector<unsigned> wused,rob,transit,completion,rmw_used,bank_rr,read_run;
    std::vector<std::deque<std::shared_ptr<Fragment>>> ingress,bq,wbq,returns;
    std::vector<uint64_t> bank_rnext,bank_wnext,scrub_row;
    std::map<std::pair<unsigned,uint64_t>,Fragment*> locks,write_hazards;
    std::map<std::pair<unsigned,uint64_t>,unsigned> errors;
    std::vector<Event> events;
    std::vector<std::deque<EccWork>> ecc;
    std::vector<uint64_t> ecc_next;
    std::vector<uint64_t> pop_read_at,pop_write_at,push_w_at;
    explicit Impl(Config cfg):c(cfg),mapper(c),fabric(c),reads(c.ports),writes(c.ports),wfifo(c.ports),wused(c.ports),rob(c.ports),
        transit(c.banks),completion(c.banks),rmw_used(c.banks),bank_rr(c.banks),read_run(c.banks),ingress(c.ports),bq(c.banks),wbq(c.banks),returns(c.banks),
        bank_rnext(c.banks),bank_wnext(c.banks),scrub_row(c.banks),ecc(2*(c.ecc_group?c.groups:c.banks)),ecc_next(ecc.size()),pop_read_at(c.ports,UINT64_MAX),pop_write_at(c.ports,UINT64_MAX),push_w_at(c.ports,UINT64_MAX){
        c.validate();if(c.full_data)storage=std::make_unique<aix::esl::ByteStore>(c.capacity);
        m.bank_grants.resize(c.banks);m.port_bytes.resize(c.ports);
    }
    void log(const char* event,const std::shared_ptr<Fragment>& f={}){
        if(!observer||trace_count++>=c.trace_limit)return;std::ostringstream s;s<<"{\"cycle\":"<<now<<",\"event\":\""<<event<<"\"";
        if(f){s<<",\"bank\":"<<f->slice.bank<<",\"word\":"<<f->slice.word<<",\"port\":"<<f->port;
            if(f->beat){auto t=f->beat->tx.lock();if(t)s<<",\"transaction\":"<<t->token<<",\"beat\":"<<f->beat->index
                <<",\"address\":"<<t->r.address+uint64_t(f->beat->index)*t->r.beat_bytes+f->slice.offsets.front();}}
        s<<"}";observer(s.str());
    }
    bool head(const std::shared_ptr<Transaction>& t)const{
        const auto& q=t->r.write?writes[t->r.port]:reads[t->r.port];
        for(auto token:q){auto x=txs.at(token);if(x->r.id==t->r.id)return x==t;}return false;
    }
    unsigned effective_mask(const std::shared_ptr<Fragment>& f,unsigned i)const{return !f->write||f->beat->mask[f->slice.offsets[i]];}
    void describe(const std::shared_ptr<Fragment>& f){
        unsigned granule=c.ecc_bytes?c.ecc_bytes:(c.macro_word_write?c.word_bytes:1);std::map<unsigned,unsigned> counts;
        for(size_t i=0;i<f->slice.lanes.size();++i)if(effective_mask(f,i))counts[f->slice.lanes[i]/granule]++;
        for(auto kv:counts){f->touched.push_back(kv.first);if(f->write&&kv.second<granule)f->rmw=true;}
        if(c.macro_word_write&&f->write){unsigned valid=0;for(auto kv:counts)valid+=kv.second;if(valid&&valid<c.word_bytes)f->rmw=true;}
        f->codewords=counts.size();if(c.macro_word_write&&f->rmw&&c.ecc_bytes)f->codewords=c.word_bytes/c.ecc_bytes;
    }
    bool conflicts(const std::shared_ptr<Fragment>& f)const{
        auto b=f->slice.bank;auto w=f->slice.word;auto wh=write_hazards.find({b,w});if(wh!=write_hazards.end()&&wh->second!=f.get())return true;
        unsigned granule=c.ecc_bytes?c.ecc_bytes:(c.macro_word_write?c.word_bytes:1);
        for(auto cw:f->touched){auto it=locks.find({b,w*(c.word_bytes/granule)+cw});if(it!=locks.end()&&it->second!=f.get())return true;}
        return false;
    }
    bool lock(const std::shared_ptr<Fragment>& f){
        if(!f->rmw||f->locked)return true;auto b=f->slice.bank;if(rmw_used[b]>=c.rmw_contexts||conflicts(f))return false;
        ++rmw_used[b];f->locked=true;++m.rmw;unsigned granule=c.ecc_bytes?c.ecc_bytes:(c.macro_word_write?c.word_bytes:1);
        auto touched=f->touched;if(c.macro_word_write){touched.clear();for(unsigned i=0;i<c.word_bytes/granule;++i)touched.push_back(i);}
        for(auto cw:touched)locks[{b,f->slice.word*(c.word_bytes/granule)+cw}]=f.get();return true;
    }
    void unlock(const std::shared_ptr<Fragment>& f){if(!f->locked)return;for(auto it=locks.begin();it!=locks.end();)if(it->second==f.get())it=locks.erase(it);else++it;--rmw_used[f->slice.bank];f->locked=false;}
    void later(uint64_t delay,std::function<void()> fn){events.push_back({now+delay,std::move(fn)});}
    unsigned engine(const std::shared_ptr<Fragment>& f,bool encode)const{return 2*(c.ecc_group?f->slice.bank/(c.banks/c.groups):f->slice.bank)+unsigned(encode);}
    bool ecc_room(const std::shared_ptr<Fragment>& f,bool encode)const{return !c.ecc_bytes||ecc[engine(f,encode)].size()<c.completion_entries*(c.ecc_group?c.banks/c.groups:1)+c.rmw_contexts;}
    void enqueue_ecc(const std::shared_ptr<Fragment>& f,bool encode,std::function<void()> done){
        f->stage=3;if(!c.ecc_bytes){later(1,std::move(done));return;}ecc[engine(f,encode)].push_back({f,f->codewords,encode,std::move(done)});
    }
    void finish(const std::shared_ptr<Fragment>& f){
        auto b=f->slice.bank;unlock(f);auto wh=write_hazards.find({b,f->slice.word});if(wh!=write_hazards.end()&&wh->second==f.get())write_hazards.erase(wh);
        f->stage=4;f->ready=now;
        if(f->scrub){if(f->slot){--completion[b];f->slot=false;}f->stage=5;++m.fragments_done;return;}
        returns[b].push_back(f);
    }
    void decode_done(const std::shared_ptr<Fragment>& f){
        bool corrected=false;if(c.ecc_bytes)for(auto cw:f->touched){auto key=std::make_pair(f->slice.bank,f->slice.word*(c.word_bytes/c.ecc_bytes)+cw);auto it=errors.find(key);
            if(it!=errors.end()&&it->second){if(it->second==1){++m.corrected;corrected=true;}else{++m.uncorrectable;f->error=true;}}}
        if(f->error){if(f->beat){f->beat->error=true;std::fill(f->beat->data.begin(),f->beat->data.end(),0xDE);}finish(f);return;}
        auto done=[this,f](){if(f->rmw){enqueue_ecc(f,true,[this,f](){f->stage=1;f->ready=now;wbq[f->slice.bank].push_back(f);});}else finish(f);};
        // Correctable errors are an explicit performance fault abstraction, not gate-level SECDED.
        if(corrected)later(c.correction_latency,done);else done();
    }
    void sample(const std::shared_ptr<Fragment>& f){
        if(!storage)return;auto base=f->slice.bank*(c.capacity/c.banks)+f->slice.word*c.word_bytes;
        if(f->rmw){f->old.resize(c.word_bytes);for(unsigned i=0;i<c.word_bytes;++i)f->old[i]=(*storage)[base+i];}
        if(f->beat&&!f->write)for(size_t i=0;i<f->slice.lanes.size();++i)f->beat->data[f->slice.offsets[i]]=(*storage)[base+f->slice.lanes[i]];
    }
    void write_commit(const std::shared_ptr<Fragment>& f){
        auto base=f->slice.bank*(c.capacity/c.banks)+f->slice.word*c.word_bytes;
        if(f->beat&&f->write)for(size_t i=0;i<f->slice.lanes.size();++i)if(effective_mask(f,i)){
            if(storage)(*storage)[base+f->slice.lanes[i]]=f->beat->data[f->slice.offsets[i]];++m.write_bytes;}
        if(c.ecc_bytes)for(auto cw:f->touched)errors.erase({f->slice.bank,f->slice.word*(c.word_bytes/c.ecc_bytes)+cw});
        log("write_commit",f);finish(f);
    }
    void issue(const std::shared_ptr<Fragment>& f,bool write){
        auto b=f->slice.bank;f->stage=2;++m.bank_services;++m.bank_grants[b];log(write?"bank_write":"bank_read",f);
        if(!f->slot){++completion[b];f->slot=true;}
        if(write){write_hazards[{b,f->slice.word}]=f.get();bank_wnext[b]=now+c.bank_ii;if(!c.dual_port)bank_rnext[b]=bank_wnext[b];
            later(c.write_latency,[this,f](){write_commit(f);});}
        else{bank_rnext[b]=now+c.bank_ii;if(!c.dual_port)bank_wnext[b]=bank_rnext[b];sample(f);
            later(c.read_latency,[this,f](){if(c.ecc_bytes)enqueue_ecc(f,false,[this,f](){decode_done(f);});else if(f->rmw)enqueue_ecc(f,true,[this,f](){f->stage=1;f->ready=now;wbq[f->slice.bank].push_back(f);});else finish(f);});}
    }
    void frontend(){
        for(unsigned p=0;p<c.ports;++p){
            // AW/W association is AW order across IDs, one W per cycle.
            if(!wfifo[p].empty())for(auto token:writes[p]){auto t=txs.at(token);if(t->next_w==t->r.beats)continue;auto w=std::move(wfifo[p].front());wfifo[p].pop_front();
                auto b=t->beats[t->next_w];if(w.data.size()!=t->r.beat_bytes||w.last!=(t->next_w+1==t->r.beats))throw std::runtime_error("W size/WLAST mismatch");
                b->data=std::move(w.data);b->mask=std::move(w.mask);b->w_ready=true;++t->next_w;break;}
            for(bool writing:{false,true}){auto& q=writing?writes[p]:reads[p];
                for(auto token:q){auto t=txs.at(token);if(!head(t)||t->next_generate>=t->r.beats)continue;auto b=t->beats[t->next_generate];if(writing&&!b->w_ready)continue;
                    if(t->next_generate&&t->beats[t->next_generate-1]->unsent)continue;
                    if(!writing&&rob[p]>=c.rob_beats){++m.rob_stall;continue;}
                    auto slices=mapper.split(t->r.address+t->next_generate*t->r.beat_bytes,t->r.beat_bytes);
                    if(ingress[p].size()+slices.size()>c.ingress_entries){++m.frontend_stall;continue;}
                    b->generated=true;if(!writing)++rob[p];
                    for(auto& s:slices){auto f=std::make_shared<Fragment>();f->beat=b;f->slice=std::move(s);f->port=p;f->born=now;f->ready=now+1;f->write=writing;describe(f);
                        if(f->touched.empty())continue;ingress[p].push_back(f);++b->pending;++b->unsent;++m.fragments;}
                    if(!b->pending){b->done=true;if(writing)--wused[p];}++t->next_generate;
                    m.queue_peak=std::max<uint64_t>(m.queue_peak,ingress[p].size());m.rob_peak=std::max<uint64_t>(m.rob_peak,rob[p]);break;
                }
            }
        }
    }
    void route_requests(){
        for(unsigned rank=0;rank<c.ports;++rank){unsigned p=(rank+now)%c.ports;unsigned sent_r=0,sent_w=0;std::set<unsigned> blocked_groups;
            for(auto it=ingress[p].begin();it!=ingress[p].end();){auto f=*it;auto b=f->slice.bank;unsigned& sent=f->write?sent_w:sent_r;
                bool eligible=f->ready<=now&&sent<c.lanes&&transit[b]+bq[b].size()<c.bank_entries;
                unsigned group=b/(c.banks/c.groups);if(c.queue=="group_voq"&&blocked_groups.count(group))eligible=false;
                if(eligible){unsigned bytes=f->write?(*std::max_element(f->slice.lanes.begin(),f->slice.lanes.end())-*std::min_element(f->slice.lanes.begin(),f->slice.lanes.end())+1):0;
                    eligible=fabric.submit(p,b,f->write?1:0,bytes,now,[this,f](){--transit[f->slice.bank];f->ready=now;bq[f->slice.bank].push_back(f);});}
                if(!eligible){blocked_groups.insert(group);if(c.queue=="fifo"){
                        if(std::any_of(std::next(it),ingress[p].end(),[&](auto x){return x->ready<=now&&transit[x->slice.bank]+bq[x->slice.bank].size()<c.bank_entries;}))++m.hol;break;}++it;continue;}
                ++transit[b];++sent;--f->beat->unsent;if(f->write&&!f->beat->unsent)--wused[p];log("dispatch",f);it=ingress[p].erase(it);
            }
        }
    }
    void banks(){
        for(unsigned b=0;b<c.banks;++b){m.bank_peak=std::max<uint64_t>(m.bank_peak,bq[b].size());
            for(auto& f:bq[b])if(f->stage==0&&f->ready<=now){
                if(c.ecc_bytes&&!f->write)for(auto cw:f->touched){auto it=errors.find({b,f->slice.word*(c.word_bytes/c.ecc_bytes)+cw});
                    if(it!=errors.end()&&it->second==1){f->repair=true;f->rmw=true;}}
                if(f->write&&!f->rmw&&c.ecc_bytes){if(ecc_room(f,true))enqueue_ecc(f,true,[this,f](){f->stage=1;f->ready=now;});}
                else f->stage=1;
            }
            unsigned count=0;for(auto& f:bq[b])if(f->stage==1&&f->ready<=now&&!conflicts(f)
                &&(f->write&&!f->rmw?bank_wnext[b]:bank_rnext[b])<=now
                &&(f->slot||completion[b]<c.completion_entries))++count;
            if(count>1){++m.bank_conflicts;m.conflict_wait+=count-1;}
            std::set<uint64_t> serviced_words;
            for(unsigned grant=0;grant<(c.dual_port?2u:1u);++grant){std::shared_ptr<Fragment> best;bool best_wb=false;uint64_t best_score=UINT64_MAX;
                auto examine=[&](auto& q,bool wb){for(auto& f:q){if(f->stage!=1||f->ready>now)continue;
                    bool wr=wb||(f->write&&!f->rmw);if((wr?bank_wnext[b]:bank_rnext[b])>now||serviced_words.count(f->slice.word))continue;
                    if(!f->slot&&completion[b]>=c.completion_entries)continue;if(conflicts(f)){++m.lock_wait;continue;}
                    if(f->rmw&&!f->locked&&rmw_used[b]>=c.rmw_contexts)continue;if(!wr&&!ecc_room(f,false))continue;
                    uint64_t age=now-f->born,score=(uint64_t(1)<<60)+(f->port+c.ports-bank_rr[b]%c.ports)%c.ports;
                    if(c.arbitration=="weighted"){
                        unsigned turns=c.ports+c.ports/2,distance=turns;
                        for(unsigned i=0;i<turns;++i){unsigned ticket=(bank_rr[b]+i)%turns;unsigned port=ticket<2*(c.ports/2)?ticket/2:c.ports/2+ticket-2*(c.ports/2);
                            if(port==f->port){distance=i;break;}}score=(uint64_t(1)<<60)+distance;
                    }
                    if(c.arbitration=="read_first")score+=((wr&&read_run[b]<c.max_read_grants)||(!wr&&read_run[b]>=c.max_read_grants))?128:0;
                    if(f->scrub)score=(uint64_t(2)<<60)+f->born;
                    if(c.arbitration=="age"||age>=uint64_t(c.age_guard)*(f->scrub?16:1)||wb)score=f->born;
                    if(score<best_score){best=f;best_score=score;best_wb=wb;}
                }};examine(wbq[b],true);examine(bq[b],false);if(!best)break;
                if(!lock(best))break;bool wr=best_wb||(best->write&&!best->rmw);auto& q=best_wb?wbq[b]:bq[b];q.erase(std::find(q.begin(),q.end(),best));
                if(wr)read_run[b]=0;else++read_run[b];
                if(c.arbitration=="weighted"){
                    unsigned turns=c.ports+c.ports/2;for(unsigned i=0;i<turns;++i){unsigned ticket=(bank_rr[b]+i)%turns;unsigned port=ticket<2*(c.ports/2)?ticket/2:c.ports/2+ticket-2*(c.ports/2);
                        if(port==best->port){bank_rr[b]=(ticket+1)%turns;break;}}
                }else bank_rr[b]=(best->port+1)%c.ports;
                serviced_words.insert(best->slice.word);issue(best,wr);
            }
        }
    }
    void ecc_tick(){
        for(size_t e=0;e<ecc.size();++e){if(ecc_next[e]>now||ecc[e].empty())continue;
            unsigned budget=c.ecc_lanes*(c.ecc_group?c.banks/c.groups:1);if(c.ecc_group)budget=std::min(budget,256/c.ecc_bytes);
            bool used=false;while(budget&&!ecc[e].empty()){auto& work=ecc[e].front();unsigned n=std::min(budget,work.remaining);work.remaining-=n;budget-=n;used=true;
                if(!work.remaining){auto done=std::move(work.done);ecc[e].pop_front();later(c.ecc_latency+(c.ecc_group?2:0),std::move(done));}else break;}
            if(used)ecc_next[e]=now+c.ecc_ii;
        }
    }
    void route_returns(){
        for(unsigned b=0;b<c.banks;++b){for(unsigned grant=0;grant<(c.dual_port?2u:1u);++grant){if(returns[b].empty())break;auto f=returns[b].front();
            unsigned bytes=f->write?8:f->slice.offsets.size();
            if(fabric.submit(f->port,b,f->write?3:2,bytes,now,[this,f](){
                auto beat=f->beat;beat->error|=f->error;if(!--beat->pending)beat->done=true;f->stage=5;++m.fragments_done;
                later(c.credit_delay,[this,f](){--completion[f->slice.bank];f->slot=false;});log("fragment_return",f);
            }))returns[b].pop_front();else break;}
        }
    }
    void scrub_tick(){
        if(!scrubbing||!c.scrub_interval||now%c.scrub_interval)return;
        for(unsigned b=0;b<c.banks;++b){if(bq[b].size()+transit[b]>=c.bank_entries)continue;
            auto f=std::make_shared<Fragment>();f->scrub=true;f->slice.bank=b;f->slice.word=scrub_row[b]++%(c.capacity/c.banks/c.word_bytes);f->born=now;f->ready=now+1;
            unsigned n=c.ecc_bytes?c.ecc_bytes:c.word_bytes;for(unsigned i=0;i<n;++i){f->slice.lanes.push_back(i);f->slice.offsets.push_back(i);}describe(f);bq[b].push_back(f);++m.scrub;++m.fragments;
        }
    }
    bool idle()const{
        if(!txs.empty()||!events.empty()||!fabric.idle())return false;
        for(auto& q:ecc)if(!q.empty())return false;
        for(unsigned b=0;b<c.banks;++b)if(!bq[b].empty()||!wbq[b].empty()||!returns[b].empty())return false;
        for(auto& q:wfifo)if(!q.empty())return false;return true;
    }
    void tick(){
        ++now;if(now>c.cycle_limit)throw std::runtime_error("simulation cycle limit");
        // Events added by callbacks cannot invalidate the event iteration.
        auto old=std::move(events);events.clear();for(auto& e:old)if(e.due<=now)e.fn();else events.push_back(std::move(e));
        fabric.tick(now);ecc_tick();banks();route_returns();route_requests();frontend();scrub_tick();
        m.link_stall=fabric.stalls;m.network_bytes=fabric.transferred;m.remote_bytes=fabric.remote;
        if(m.accepted!=m.completed+txs.size()||m.fragments_done>m.fragments)throw std::logic_error("conservation");
        for(unsigned p=0;p<c.ports;++p)if(rob[p]>c.rob_beats||wused[p]>c.w_beats||ingress[p].size()>c.ingress_entries)throw std::logic_error("frontend capacity");
        for(unsigned b=0;b<c.banks;++b)if(bq[b].size()+transit[b]>c.bank_entries||completion[b]>c.completion_entries||rmw_used[b]>c.rmw_contexts)throw std::logic_error("bank capacity");
    }
};
Model::Model(sc_core::sc_module_name name,const Config& c):sc_module(name),p_(std::make_unique<Impl>(c)){SC_THREAD(run);}
Model::~Model()=default;
void Model::run(){while(true){wait(1,sc_core::SC_NS);p_->tick();}}
uint64_t Model::cycle()const{return p_->now;}
const Metrics& Model::metrics()const{return p_->m;}
uint64_t Model::submit(const Request& r){auto& p=*p_;auto& c=p.c;
    if(r.port>=c.ports||r.id>=c.ids||!r.beats||r.beats>32||r.beat_bytes<8||r.beat_bytes>128||(r.beat_bytes&(r.beat_bytes-1))||r.address%r.beat_bytes||r.release>p.now)throw std::invalid_argument("AXI request");
    uint64_t length=uint64_t(r.beats)*r.beat_bytes;if(r.address/4096!=(r.address+length-1)/4096||r.address>c.capacity||length>c.capacity-r.address)throw std::invalid_argument("AXI range/4KiB");
    for(unsigned i=0;i<length;++i)p.mapper.map(r.address+i);
    auto& q=r.write?p.writes[r.port]:p.reads[r.port];if(q.size()>=c.outstanding){++p.m.frontend_stall;return 0;}
    // One address handshake per port/direction per cycle.
    if(!q.empty()&&p.txs.at(q.back())->accepted==p.now)return 0;
    auto t=std::make_shared<Transaction>();t->r=r;t->token=p.next_token++;t->accepted=p.now;
    for(unsigned i=0;i<r.beats;++i){auto b=std::make_shared<Beat>();b->tx=t;b->index=i;b->data.resize(r.beat_bytes);b->mask.assign(r.beat_bytes,1);t->beats.push_back(b);}
    p.txs[t->token]=t;q.push_back(t->token);++p.m.accepted;p.m.admission_latencies.push_back(p.now-r.release);return t->token;
}
bool Model::push_w(unsigned port,const std::vector<uint8_t>& data,const std::vector<uint8_t>& mask,bool last){auto& p=*p_;
    if(port>=p.c.ports||data.size()!=mask.size()||data.empty()||data.size()>128||std::any_of(mask.begin(),mask.end(),[](auto x){return x>1;}))throw std::invalid_argument("W payload/mask");
    if(p.wused[port]>=p.c.w_beats||p.push_w_at[port]==p.now)return false;p.wfifo[port].push_back({data,mask,last});++p.wused[port];p.push_w_at[port]=p.now;return true;
}
bool Model::pop(unsigned port,bool write,Response& out){auto& p=*p_;if(port>=p.c.ports)throw std::invalid_argument("port");
    auto& last=write?p.pop_write_at[port]:p.pop_read_at[port];if(last==p.now)return false;auto& q=write?p.writes[port]:p.reads[port];
    for(auto it=q.begin();it!=q.end();++it){auto t=p.txs.at(*it);if(!p.head(t))continue;
        auto b=t->beats[t->next_response];if(write){if(!std::all_of(t->beats.begin(),t->beats.end(),[](auto x){return x->done;}))continue;}
        else if(!b->done)continue;
        out={t->token,t->accepted,p.now,port,t->r.id,t->next_response,write,write||t->next_response+1==t->r.beats,false,{}};
        if(write){for(auto& beat:t->beats){out.error|=beat->error;for(auto mask:beat->mask)t->bytes+=mask;}p.m.external_write_bytes+=t->bytes;}
        else{out.error=b->error;out.data=b->data;t->bytes+=t->r.beat_bytes;p.m.read_bytes+=t->r.beat_bytes;p.later(p.c.credit_delay,[&p,port](){--p.rob[port];});}
        t->error|=out.error;out.error=t->error;++t->next_response;last=p.now;
        if(out.last){p.m.port_bytes[port]+=t->bytes;p.m.latencies.push_back(p.now-t->accepted);++p.m.completed;q.erase(it);p.txs.erase(t->token);}return true;
    }return false;
}
bool Model::idle()const{return p_->idle();}
void Model::stop_scrub(){p_->scrubbing=false;}
void Model::initialize(uint64_t address,const std::vector<uint8_t>& data){
    if(!idle()||!p_->storage)throw std::logic_error("debug access requires idle full_data SRAM");
    if(address>p_->c.capacity||data.size()>p_->c.capacity-address)throw std::invalid_argument("debug range");
    std::vector<uint64_t> physical;
    for(size_t i=0;i<data.size();++i){auto x=p_->mapper.map(address+i);physical.push_back(x.bank*(p_->c.capacity/p_->c.banks)+x.local);}
    for(size_t i=0;i<data.size();++i)(*p_->storage)[physical[i]]=data[i];
}
std::vector<uint8_t> Model::inspect(uint64_t address,unsigned bytes)const{
    if(!idle()||!p_->storage)throw std::logic_error("debug access requires idle full_data SRAM");
    if(address>p_->c.capacity||bytes>p_->c.capacity-address)throw std::invalid_argument("debug range");
    std::vector<uint8_t> data(bytes);
    for(unsigned i=0;i<bytes;++i){auto x=p_->mapper.map(address+i);data[i]=(*p_->storage)[x.bank*(p_->c.capacity/p_->c.banks)+x.local];}
    return data;
}
void Model::reset(){if(!idle())throw std::logic_error("reset requires drain");auto cfg=p_->c;auto observe=p_->observer;p_=std::make_unique<Impl>(cfg);p_->observer=observe;}
void Model::inject(uint64_t a,unsigned n){if(!p_->c.ecc_bytes||n>2)throw std::invalid_argument("ECC injection");auto x=p_->mapper.map(a);p_->errors[{x.bank,x.local/p_->c.ecc_bytes}]=n;}
void Model::set_observer(std::function<void(const std::string&)> fn){p_->observer=std::move(fn);}
std::string Model::snapshot()const{std::ostringstream s;s<<"cycle="<<p_->now<<" tx="<<p_->txs.size()<<" network="<<p_->fabric.packets()<<" locks="<<p_->locks.size();for(unsigned b=0;b<p_->c.banks;++b)if(!p_->bq[b].empty())s<<" bank"<<b<<"="<<p_->bq[b].size();return s.str();}
void Model::report(std::ostream& o)const{auto& p=*p_;auto& m=p.m;auto latency=m.latencies;std::sort(latency.begin(),latency.end());
    auto pct=[&](unsigned n){return latency.empty()?0:latency[(latency.size()-1)*n/100];};
    o<<"{\"status\":\""<<(idle()&&m.accepted==m.completed&&m.fragments==m.fragments_done?"PASS":"FAIL")<<"\",\"cycles\":"<<p.now;
#define MET(x) o<<",\"" #x "\":"<<m.x
    MET(accepted);MET(completed);MET(fragments);MET(fragments_done);MET(read_bytes);MET(write_bytes);MET(external_write_bytes);MET(bank_services);MET(rmw);MET(scrub);MET(corrected);MET(uncorrectable);MET(bank_conflicts);MET(conflict_wait);MET(lock_wait);MET(hol);MET(frontend_stall);MET(rob_stall);MET(link_stall);MET(remote_bytes);MET(queue_peak);MET(bank_peak);MET(rob_peak);MET(network_bytes);
#undef MET
    o<<",\"p50\":"<<pct(50)<<",\"p95\":"<<pct(95)<<",\"p99\":"<<pct(99)<<",\"latency_samples\":"<<latency.size()<<",\"max_latency\":"<<(latency.empty()?0:latency.back());
    o<<",\"payload_bytes_per_cycle\":"<<double(m.read_bytes+m.write_bytes)/std::max<uint64_t>(1,p.now);
    o<<",\"bank_utilization\":"<<double(m.bank_services*p.c.bank_ii)/std::max<uint64_t>(1,p.now*p.c.banks*(p.c.dual_port?2:1));
    o<<",\"sram_amplification\":"<<double(m.bank_services*p.c.word_bytes)/std::max<uint64_t>(1,m.read_bytes+m.write_bytes);
    auto array=[&](const char* name,auto& values){o<<",\""<<name<<"\":[";for(size_t i=0;i<values.size();++i){if(i)o<<",";o<<values[i];}o<<"]";};array("bank_grants",m.bank_grants);array("port_bytes",m.port_bytes);array("latencies",m.latencies);o<<"}";
}
}
