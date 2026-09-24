#include "target.hpp"
#include <algorithm>
#include <stdexcept>
namespace aix::esl::npu_mesh {
MemoryTarget::MemoryTarget(const Endpoint& e,unsigned index):cfg(e) {
    if(e.backend=="sram") {
        auto config=e.sram; config.capacity=e.size; config.full_data=1;
        config.scrub_interval=0;
        sram=std::make_unique<npu_sram_controller::Model>(
            ("sram_endpoint_"+std::to_string(index)).c_str(),config);
        sram->stop_scrub(); writes.resize(config.ports);
    } else store=std::make_unique<aix::esl::ByteStore>(e.size);
}
std::shared_ptr<Packet> MemoryTarget::response(const std::shared_ptr<Packet>& p)const {
    auto r=std::make_shared<Packet>(*p);
    std::swap(r->source,r->destination); r->vn=p->vn==0?2:3;
    return r;
}
void MemoryTarget::accept(std::shared_ptr<Packet> p,std::uint64_t now) {
    Work work;work.packet=p;work.arrived=now;work.sequence=sequence++;
    if(!sram) {
        bool write=p->vn==1;
        auto start=std::max(now,next_issue);
        if(have_last&&write!=last_write){start+=cfg.turnaround_cycles;++turnarounds;}
        auto transfer=1+(p->bytes-1)/cfg.bytes_per_cycle;
        next_issue=start+transfer;
        auto latency=write?cfg.write_latency:cfg.read_latency;
        work.due=next_issue+(latency?latency:cfg.latency);
        last_write=write;have_last=true;
    }
    waiting.push_back(std::move(work));
    queue_peak=std::max(queue_peak,unsigned(waiting.size()+issued.size()));
}
void MemoryTarget::tick(std::uint64_t now,bool stalled,const std::function<bool(std::uint64_t)>& stopped) {
    if(!sram) {
        if(stalled){for(auto& w:waiting)++w.due;if(next_issue>=now)++next_issue;}
        for(auto it=waiting.begin();it!=waiting.end();) {
            auto& w=*it;auto p=w.packet;
            if(stopped(p->handle)) {auto r=response(p);r->status=Status::aborted;complete.push_back(r);it=waiting.erase(it);continue;}
            if(stalled||w.due>now){++it;continue;}
            auto r=response(p);
            if(p->status!=Status::ok)r->status=p->status;
            else if(p->vn==1) {
                if(!store->write(p->address,p->data.data(),p->bytes,p->enables.empty()?nullptr:p->enables.data(),p->enables.size()))
                    throw std::logic_error("DDR write mapping");
            } else {r->data.resize(p->bytes);store->read(p->address,r->data.data(),p->bytes);}
            serviced+=p->bytes;complete.push_back(r);it=waiting.erase(it);
        }
        return;
    }
    // Native responses must always drain, even after timeout/reset or ingress stall.
    for(unsigned port=0;port<cfg.sram.ports;++port) for(bool write:{false,true}) {
        npu_sram_controller::Response r;
        if(!sram->pop(port,write,r))continue;
        auto found=issued.find(r.token);
        if(found==issued.end())throw std::logic_error("unknown SRAM response token");
        auto& w=found->second;w.error|=r.error;
        if(!write)w.data.insert(w.data.end(),r.data.begin(),r.data.end());
        if(!r.last)continue;
        auto result=response(w.packet);
        if(w.error)result->status=Status::target;
        else if(!write)result->data.assign(w.data.begin()+w.prefix,w.data.begin()+w.prefix+w.packet->bytes);
        serviced+=w.packet->bytes;complete.push_back(result);issued.erase(found);
    }
    // Priority applies only before native acceptance; issued AW/W/R/B never preempt.
    if(cfg.priority_context<64)std::stable_sort(waiting.begin(),waiting.end(),[&](const Work& a,const Work& b) {
        bool aa=now-a.arrived>=cfg.age_cycles,ba=now-b.arrived>=cfg.age_cycles;
        if(aa!=ba)return aa;
        if(aa)return a.sequence<b.sequence;
        bool ap=a.packet->context==cfg.priority_context,bp=b.packet->context==cfg.priority_context;
        if(ap!=bp)return ap;
        return a.sequence<b.sequence;
    });
    for(auto it=waiting.begin();it!=waiting.end();) {
        auto& w=*it;auto p=w.packet;
        if(stopped(p->handle)||p->status!=Status::ok) {
            auto r=response(p);r->status=p->status!=Status::ok?p->status:Status::aborted;
            complete.push_back(r);it=waiting.erase(it);continue;
        }
        if(stalled||(cfg.issue_limit&&issued.size()>=cfg.issue_limit)){++it;continue;}
        // Full 128B native beats. Edge writes use masks, edge reads trim padding.
        w.aligned=p->address/128*128;w.prefix=p->address-w.aligned;
        w.beats=(w.prefix+p->bytes+127)/128;w.port=p->source%cfg.sram.ports;
        npu_sram_controller::Request r;r.port=w.port;r.id=(p->packet%cfg.sram.ids);
        r.beat_bytes=128;r.beats=w.beats;r.address=w.aligned;r.write=p->vn==1;r.release=sram->cycle();
        auto token=sram->submit(r);
        if(!token){++retries;++it;continue;}
        w.native=token;
        if(r.write)writes[w.port].push_back(token);
        issued.emplace(token,std::move(w));it=waiting.erase(it);
    }
    // W obeys accepted AW order, irrespective of IDs; exactly one beat/port/cycle.
    for(unsigned port=0;port<writes.size();++port) {
        if(writes[port].empty())continue;
        auto& w=issued.at(writes[port].front());
        std::vector<unsigned char> data(128),mask(128);
        for(unsigned i=0;i<128;++i) {
            unsigned offset=w.pushed*128+i;
            if(offset<w.prefix||offset>=w.prefix+w.packet->bytes)continue;
            unsigned n=offset-w.prefix;data[i]=w.packet->data[n];
            mask[i]=w.packet->enables.empty()||w.packet->enables[n]?1:0;
        }
        if(sram->push_w(port,data,mask,w.pushed+1==w.beats)) {
            if(++w.pushed==w.beats)writes[port].pop_front();
        }else ++retries;
    }
}
bool MemoryTarget::pop(std::shared_ptr<Packet>& p) {
    if(complete.empty())return false;p=complete.front();complete.pop_front();return true;
}
std::vector<std::shared_ptr<Packet>> MemoryTarget::reset_pending() {
    waiting.clear();complete.clear();next_issue=0;have_last=false;
    std::vector<std::shared_ptr<Packet>> pending;
    for(auto& entry:issued)pending.push_back(entry.second.packet);
    return pending;
}
bool MemoryTarget::idle()const {return waiting.empty()&&issued.empty()&&complete.empty()&&(!sram||sram->idle());}
void MemoryTarget::initialize(std::uint64_t a,const std::vector<unsigned char>& bytes) {
    if(sram)sram->initialize(a,bytes);else store->write(a,bytes.data(),bytes.size());
}
std::vector<unsigned char> MemoryTarget::inspect(std::uint64_t a,unsigned n)const {
    if(sram)return sram->inspect(a,n);
    std::vector<unsigned char> data(n);store->read(a,data.data(),n);return data;
}
void MemoryTarget::report(std::ostream& out)const {
    if(sram){sram->report(out);return;}
    out<<"{\"backend\":\"ddr_service\",\"logical_bytes\":"<<serviced
       <<",\"queue_peak\":"<<queue_peak<<",\"turnarounds\":"<<turnarounds<<"}";
}
bool MemoryTarget::same_service(const Endpoint& e)const {
    if(e.backend!=cfg.backend||e.size!=cfg.size||e.slots!=cfg.slots||e.priority_context!=cfg.priority_context||e.age_cycles!=cfg.age_cycles||e.issue_limit!=cfg.issue_limit)return false;
    if(e.backend=="ddr")return e.bytes_per_cycle==cfg.bytes_per_cycle&&e.latency==cfg.latency&&
        e.read_latency==cfg.read_latency&&e.write_latency==cfg.write_latency&&e.turnaround_cycles==cfg.turnaround_cycles;
    const auto& a=cfg.sram;const auto& b=e.sram;
#define SAME(field) if(a.field!=b.field)return false
    SAME(ports);SAME(banks);SAME(word_bytes);SAME(stripe_bytes);SAME(groups);SAME(cycle_limit);
    SAME(mapping);SAME(topology);SAME(queue);SAME(arbitration);SAME(xor_shift);SAME(group_first);SAME(local_xor);
    SAME(dual_port);SAME(dual_ingress);SAME(read_latency);SAME(write_latency);SAME(bank_ii);SAME(outstanding);
    SAME(ids);SAME(ingress_entries);SAME(bank_entries);SAME(rob_beats);SAME(w_beats);SAME(completion_entries);
    SAME(rmw_contexts);SAME(lanes);SAME(return_bytes);SAME(remote_bytes);SAME(link_entries);SAME(link_buffer_bytes);
    SAME(link_latency);SAME(credit_delay);SAME(matching_rounds);SAME(age_guard);SAME(max_read_grants);
    SAME(ecc_bytes);SAME(ecc_lanes);SAME(ecc_ii);SAME(ecc_latency);SAME(ecc_group);SAME(macro_word_write);
    SAME(scrub_interval);SAME(correction_latency);SAME(trace_limit);
#undef SAME
    return true;
}
}
