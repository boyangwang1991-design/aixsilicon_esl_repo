#pragma once
#include "npu_sram_controller/config.hpp"
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace aix::esl::npu_sram_controller::detail {
// Store-and-forward network. Every hop has bounded waiting+reserved entries/bytes.
struct Packet {
    unsigned source=0,bank=0,kind=0,bytes=0,hop=0;
    uint64_t ready=0;
    std::vector<std::string> route;
    std::function<void()> delivered;
};
class Fabric {
    struct Link {unsigned width=0,slots=0,reserved=0,reserved_bytes=0;uint64_t next=0;unsigned rr=0;
        std::deque<std::shared_ptr<Packet>> waiting;};
    struct Flight{uint64_t due;std::shared_ptr<Packet> packet;};
    Config c;std::map<std::string,Link> links;std::vector<Flight> flight;
    unsigned pgroup(unsigned p)const{return p/(c.ports/c.groups);}
    unsigned bgroup(unsigned b)const{return b/(c.banks/c.groups);}
    void link(const std::string& name,unsigned width,unsigned slots){auto& l=links[name];l.width=width;l.slots=slots;}
    bool room(const Link& l,unsigned bytes)const{return l.waiting.size()+l.reserved<c.link_entries&&queued_bytes(l)+l.reserved_bytes+bytes<=c.link_buffer_bytes;}
    static unsigned queued_bytes(const Link& l){unsigned n=0;for(auto& p:l.waiting)n+=p->bytes;return n;}
public:
    uint64_t stalls=0,transferred=0,remote=0;
    explicit Fabric(const Config& cfg):c(cfg){}
    bool submit(unsigned port,unsigned bank,unsigned kind,unsigned bytes,uint64_t now,std::function<void()> done){
        auto p=std::make_shared<Packet>();p->source=port;p->bank=bank;p->kind=kind;p->bytes=bytes;p->ready=now+1;p->delivered=std::move(done);
        if(c.topology=="ideal"){flight.push_back({now+2,p});return true;}
        // kind 0 read command, 1 write command/data, 2 read response, 3 write completion.
        const bool response=kind>=2;auto prefix=response?(kind==2?"r":"b"):(kind==0?"a":"w");
        unsigned sg=response?bgroup(bank):pgroup(port),dg=response?pgroup(port):bgroup(bank);
        auto first=std::string(prefix)+".src."+std::to_string(response?bank:port);
        link(first,response?c.word_bytes:128,response?1:c.lanes);p->route.push_back(first);
        if(c.topology=="hierarchical"&&sg!=dg){
            for(auto stage:{"up.","mid.","down."}){
                auto name=std::string(prefix)+stage+std::to_string(std::string(stage)=="up."?sg:dg);
                link(name,c.remote_bytes,std::max(1u,256/c.word_bytes));p->route.push_back(name);
            }
        }
        std::string last;
        if(response){last=std::string(prefix)+".port."+std::to_string(port);link(last,kind==2?c.return_bytes:8*c.lanes,c.lanes);}
        else if(c.topology=="hierarchical"){
            // Reads and writes share the destination group command slots.
            last="request.group."+std::to_string(dg);link(last,256,256/c.word_bytes);
        }else{last="request.bank."+std::to_string(bank);link(last,c.word_bytes,c.dual_ingress?2:1);}
        p->route.push_back(last);auto& l=links[first];if(!room(l,bytes))return false;
        l.waiting.push_back(p);if(sg!=dg&&c.topology=="hierarchical")remote+=bytes;return true;
    }
    void tick(uint64_t now){
        for(size_t i=0;i<flight.size();){if(flight[i].due>now){++i;continue;}auto p=flight[i].packet;
            if(p->route.empty()||p->hop==p->route.size())p->delivered();
            else{auto& l=links.at(p->route[p->hop]);--l.reserved;l.reserved_bytes-=p->bytes;p->ready=now;l.waiting.push_back(p);}
            flight[i]=std::move(flight.back());flight.pop_back();
        }
        std::vector<unsigned> endpoint_read(c.banks),endpoint_write(c.banks);
        for(auto& kv:links){auto& l=kv.second;if(l.next>now)continue;unsigned used=0,slots=0;
            for(unsigned round=0;round<c.matching_rounds;++round){bool progress=false;
                for(unsigned rank=0;rank<c.ports;++rank){unsigned port=(l.rr+rank)%c.ports;
                    for(auto it=l.waiting.begin();it!=l.waiting.end()&&slots<l.slots;){auto p=*it;if(p->source!=port||p->ready>now){++it;continue;}
                        unsigned charge=std::min(p->bytes,l.width);bool last=p->hop+1==p->route.size();
                        if(used+charge>l.width){++it;continue;}
                        if(last&&p->kind<2){auto b=p->bank;if((!c.dual_ingress&&(endpoint_read[b]+endpoint_write[b]))||(p->kind==0?endpoint_read[b]:endpoint_write[b])){++it;continue;}}
                        if(!last){auto& next=links.at(p->route[p->hop+1]);if(!room(next,p->bytes)){++stalls;++it;continue;}++next.reserved;next.reserved_bytes+=p->bytes;}
                        if(last&&p->kind<2)(p->kind==0?endpoint_read[p->bank]:endpoint_write[p->bank])++;
                        unsigned serial=std::max(1u,(p->bytes+l.width-1)/l.width);
                        if(serial>1)l.next=now+serial;used+=charge;++slots;transferred+=p->bytes;
                        ++p->hop;flight.push_back({now+c.link_latency+serial-1,p});it=l.waiting.erase(it);progress=true;
                        if(serial>1)break;
                    }if(l.next>now)break;
                }if(!progress||slots==l.slots||l.next>now)break;
            }l.rr=(l.rr+1)%c.ports;
        }
    }
    bool idle()const{if(!flight.empty())return false;for(auto& x:links)if(!x.second.waiting.empty())return false;return true;}
    size_t packets()const{size_t n=flight.size();for(auto& x:links)n+=x.second.waiting.size();return n;}
};
}
