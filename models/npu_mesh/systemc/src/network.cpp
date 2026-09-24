#include "network.hpp"
#include <algorithm>
#include <stdexcept>
namespace aix::esl::npu_mesh {
Network::Network(Config c, Metrics& m): cfg(c), metrics(m), routers(c.columns*c.rows),
 lanes(4*c.vcs_per_vn), channels(c.split?3:1), buffers(routers*5*lanes),
 injection(routers), rr(routers*5*channels,0) {
    for(unsigned r=0;r<routers;++r)for(unsigned o=0;o<5;++o)for(unsigned v=0;v<4;++v)ports.push_back({r,o,v});
    reset();
 }
unsigned Network::index(unsigned r,unsigned p,unsigned v) const { return (r*5+p)*lanes+v; }
unsigned Network::width(unsigned vn) const {return cfg.split && (vn==0||vn==3)?cfg.control_bytes:cfg.link_bytes;}
unsigned Network::route(unsigned r,unsigned d) const {
    if(r==d) return 4;
    if(cfg.centralized) return 0;
    if(r%cfg.columns<d%cfg.columns) return 0;
    if(r%cfg.columns>d%cfg.columns) return 1;
    return r/cfg.columns<d/cfg.columns?2:3;
}
unsigned Network::next_buffer(unsigned r,unsigned out,unsigned v) const {
    unsigned d = out==0?r+1:out==1?r-1:out==2?r+cfg.columns:r-cfg.columns;
    unsigned port = out==0?1:out==1?0:out==2?3:2;
    return index(d,port,v);
}
bool Network::inject(std::shared_ptr<Packet> p) {
    auto& q=injection.at(p->source);
    if(q.size()>=cfg.outstanding*2) return false;
    p->packet=next_packet++;
    unsigned w=width(p->vn), payload=(p->vn==1||p->vn==2)?p->bytes:0;
    unsigned mask=p->enables.empty()?0:(payload+7)/8;
    unsigned count=(16+payload+mask+w-1)/w;
    metrics.injected_bytes+=count*w; metrics.header_bytes+=16; metrics.mask_bytes+=mask;
    q.push_back({p,0,count,0}); return true;
}
bool Network::injection_idle(unsigned r) const {return injection.at(r).empty();}
void Network::tick(std::uint64_t now) {
    for(auto it=credits.begin();it!=credits.end();) {
        if(it->due<=now){++buffers[it->buffer].credit;it=credits.erase(it);}else ++it;
    }
    for(auto it=transit.begin();it!=transit.end();) {
        if(it->due<=now){auto f=it->flit;f.ready=now+cfg.router_latency;
            buffers[it->buffer].fifo.push_back(f);it=transit.erase(it);}else ++it;
    }
    for(unsigned r=0;r<routers;++r){
        // One injector per physical channel; scan VNs to avoid request/response injection deadlock.
        std::vector<bool> used(channels,false);
        auto& queue=injection[r];
        for(auto it=queue.begin();it!=queue.end();) {
            auto& in=*it;unsigned vn=in.packet->vn;
            unsigned ch=cfg.split?(vn==0?0:vn==3?2:1):0;
            if(used[ch]){++it;continue;}
            if(!in.sent){
                bool found=false;
                for(unsigned k=0;k<cfg.vcs_per_vn;++k){
                    unsigned b=index(r,4,vn*cfg.vcs_per_vn+k);
                    if(!buffers[b].owner||buffers[b].owner==in.packet->packet){in.buffer=b;buffers[b].owner=in.packet->packet;found=true;break;}
                }
                if(!found){++it;continue;}
            }
            auto& b=buffers[in.buffer];
            if(!b.credit){++metrics.credit_stalls;++it;continue;}
            --b.credit;b.fifo.push_back({in.packet,in.sent,in.count,now+cfg.router_latency});used[ch]=true;
            if(++in.sent==in.count) it=queue.erase(it);else ++it;
        }
    }
    std::vector<bool> input_used(routers*5*channels,false);
    std::vector<bool> central_used(channels,false);
    for(unsigned step=0;step<routers;++step) {
      unsigned r=cfg.centralized?(step+now)%routers:step;
      for(unsigned out=0;out<5;++out) for(unsigned ch=0;ch<channels;++ch){
        auto& cursor=rr[(r*5+out)*channels+ch];
        for(unsigned scan=0;scan<5*lanes;++scan){
            unsigned pick=(cursor+scan)%(5*lanes), port=pick/lanes, lane=pick%lanes;
            auto& b=buffers[index(r,port,lane)];
            unsigned vn=lane/cfg.vcs_per_vn;
            unsigned channel=cfg.split?(vn==0?0:vn==3?2:1):0;
            if(channel!=ch||input_used[(r*5+port)*channels+ch]||b.fifo.empty()) continue;
            auto f=b.fifo.front();
            if(f.ready>now||route(r,f.packet->destination)!=out) continue;
            auto& pm=ports[(r*5+out)*4+vn];
            bool local=out==4;
            unsigned target=0;
            if(!local){
                if(cfg.centralized){if(central_used[ch])continue;target=index(f.packet->destination,0,lane);}
                else target=next_buffer(r,out,lane);
                auto& dst=buffers[target];
                if(dst.owner && dst.owner!=f.packet->packet){++metrics.allocation_stalls;++pm.allocation_stalls;continue;}
                if(!dst.credit){++metrics.credit_stalls;++pm.credit_stalls;continue;}
                dst.owner=f.packet->packet;--dst.credit;
                transit.push_back({now+cfg.link_latency,target,f});
                metrics.link_bytes+=width(vn);if(cfg.centralized)central_used[ch]=true;
            }else{
                if(f.index==0 && !admit(*f.packet)){++metrics.endpoint_stalls;++pm.endpoint_stalls;continue;}
                if(f.index+1==f.count) receive(f.packet);
            }
            ++pm.flits;pm.bytes+=width(vn);
            b.fifo.pop_front();credits.push_back({now+cfg.credit_latency,index(r,port,lane)});
            if(f.index+1==f.count)b.owner=0;
            input_used[(r*5+port)*channels+ch]=true;cursor=(pick+1)%(5*lanes);break;
        }
      }
    }
    for(const auto& b:buffers)metrics.occupancy_high=std::max(metrics.occupancy_high,unsigned(b.fifo.size()));
    check();
}
bool Network::idle() const {
    for(auto& b:buffers)if(!b.fifo.empty())return false;
    for(auto& q:injection)if(!q.empty())return false;
    return transit.empty()&&credits.empty();
}
void Network::reset(){
    for(auto& b:buffers){b.fifo.clear();b.credit=cfg.depth;b.owner=0;}
    for(auto& q:injection)q.clear();transit.clear();credits.clear();
}
void Network::check() const {
    std::vector<unsigned> occupied(buffers.size(),0);
    for(auto& t:transit)++occupied[t.buffer];for(auto& c:credits)++occupied[c.buffer];
    for(unsigned i=0;i<buffers.size();++i)
        if(buffers[i].credit+buffers[i].fifo.size()+occupied[i]!=cfg.depth)
            throw std::logic_error("credit conservation violation");
}
}
