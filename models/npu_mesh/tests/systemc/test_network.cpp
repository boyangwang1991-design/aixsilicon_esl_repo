#include "../../systemc/src/network.hpp"
#include <systemc>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace aix::esl::npu_mesh;
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static std::pair<unsigned,Metrics> stream(unsigned depth,unsigned rtt,unsigned width,bool split){
    Config c;c.columns=2;c.rows=1;c.depth=depth;c.credit_latency=rtt;c.link_bytes=width;c.split=split;c.outstanding=128;
    Metrics m;Network net(c,m);unsigned received=0;std::set<std::uint64_t> packets;
    net.admit=[](const Packet&){return true;};net.receive=[&](std::shared_ptr<Packet> p){require(packets.insert(p->packet).second,"duplicate packet");++received;};
    unsigned submitted=0,cycle=0;
    for(;cycle<100000;++cycle){
        if(submitted<64){auto p=std::make_shared<Packet>();p->source=0;p->destination=1;p->vn=1;p->bytes=256;if(net.inject(p))++submitted;}
        net.tick(cycle);if(received==64&&net.idle())break;
    }
    require(received==64,"network made no progress");
    require(m.link_bytes==64*((256+16+width-1)/width)*width,"wire byte accounting");
    require(m.link_bytes<=std::uint64_t(cycle+1)*width,"single link over capacity");
    require(m.occupancy_high<=depth,"buffer overflow");return {cycle,m};
}
int sc_main(int,char**){try{
    auto shallow=stream(1,8,32,false),deep=stream(16,8,32,false),wide=stream(16,8,64,false);
    require(deep.first<shallow.first,"credit depth does not hide RTT");require(wide.first<deep.first,"width does not improve long flow");
    // Pipeline gets within 80% of padded-flit capacity including finite train fill/drain.
    require(double(deep.second.link_bytes)/deep.first>=0.8*32,"deep pipeline not sustaining bandwidth");
    Config c;c.columns=4;c.rows=2;c.depth=4;Metrics m;Network net(c,m);unsigned received=0;std::uint64_t expected=0;
    net.admit=[](const Packet&){return true;};net.receive=[&](std::shared_ptr<Packet>){++received;};
    for(unsigned s=0;s<8;++s)for(unsigned d=0;d<8;++d){auto p=std::make_shared<Packet>();p->source=s;p->destination=d;p->vn=(s+d)%4;p->bytes=17;
        require(net.inject(p),"unexpected injection rejection");unsigned hops=unsigned(std::abs(int(s%4)-int(d%4))+std::abs(int(s/4)-int(d/4)));
        unsigned flits=(16+((p->vn==1||p->vn==2)?17:0)+31)/32;expected+=hops*flits*32;}
    for(unsigned cycle=0;cycle<10000&&(!net.idle()||received!=64);++cycle)net.tick(cycle);
    require(received==64&&net.idle(),"all-pairs deadlock");require(m.link_bytes==expected,"XY hop byte mismatch");
    std::cout<<"PASS all_pairs=64 shallow_cycles="<<shallow.first<<" deep_cycles="<<deep.first<<" wide_cycles="<<wide.first<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
