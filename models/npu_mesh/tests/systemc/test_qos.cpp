#include <npu_mesh/model.hpp>
#include "../../systemc/src/target.hpp"
#include <iostream>
#include <stdexcept>
using namespace aix::esl::npu_mesh;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int sc_main(int argc,char** argv){try{
    std::string mode=argc>1?argv[1]:"shape";
    if(mode=="shape"||mode=="shape_reset") {
        Config cfg;cfg.columns=cfg.rows=1;cfg.packet_bytes=64;cfg.fragment_window=8;
        cfg.shape_bytes_per_cycle=1;cfg.shape_burst_bytes=64;cfg.endpoints.push_back({});
        Model m("mesh",cfg);Request q;q.op=Op::write;q.context=2;q.length=256;q.data.assign(256,0x71);
        auto h=m.submit(q);check(h.status==Status::ok,"submit");
        Request fast=q;fast.context=1;fast.address=1024;auto f=m.submit(fast);Completion c,fc;bool done=false,fdone=false;
        if(mode=="shape_reset"){
            for(unsigned i=0;i<30;++i)sc_core::sc_start(1,sc_core::SC_NS);
            m.reset();
        }
        for(unsigned i=0;i<20000&&(!done||!fdone||!m.idle());++i){
            sc_core::sc_start(1,sc_core::SC_NS);
            if(!done)done=m.take(h.handle,c);if(!fdone)fdone=m.take(f.handle,fc);
        }
        check(done&&fdone&&m.idle(),"drain");
        if(mode=="shape_reset") {
            check(c.status==Status::aborted,"reset status");check(m.resume(),"resume");
            q.address=2048;auto again=m.submit(q);done=false;
            for(unsigned i=0;i<20000&&!done;++i){sc_core::sc_start(1,sc_core::SC_NS);done=m.take(again.handle,c);}
            check(done&&c.status==Status::ok,"post reset transaction");
        }else{
            check(c.status==Status::ok&&fc.status==Status::ok&&fc.done<c.done,"exemption/shape effect");
            check(m.inspect(0,256)==q.data&&m.inspect(1024,256)==q.data,"data");
            std::vector<std::uint64_t> times;
            for(auto& t:m.trace())if(t.handle==h.handle&&t.event=="INJECTED_FRAGMENT")times.push_back(t.cycle);
            check(times.size()==4,"fragment count");
            for(unsigned i=1;i<times.size();++i)check(times[i]-times[i-1]>=64,"token bucket rate violation");
            check(m.metrics().shaped_bytes==256&&m.metrics().shape_stalls>0,"shaper accounting");
        }
    }else{
        Endpoint e;e.backend="sram";e.issue_limit=1;e.priority_context=mode=="fifo"?64:1;e.age_cycles=16;
        e.sram.capacity=e.size;e.sram.full_data=1;e.sram.read_latency=20;
        MemoryTarget target(e,0);
        auto packet=[](unsigned h,unsigned context){auto p=std::make_shared<Packet>();p->packet=h;p->handle=h;p->context=context;p->bytes=128;p->vn=0;p->source=h%2;return p;};
        target.accept(packet(1,2),0);target.accept(packet(2,2),0);
        unsigned now=0;
        if(mode=="aged")for(;now<20;++now){sc_core::sc_start(1,sc_core::SC_NS);target.tick(now,true,[](auto){return false;});}
        target.accept(packet(3,1),now);target.accept(packet(4,1),now);
        std::vector<unsigned> order;
        for(;now<10000&&!target.idle();++now){
            sc_core::sc_start(1,sc_core::SC_NS);target.tick(now,false,[](auto){return false;});
            std::shared_ptr<Packet> p;while(target.pop(p)){check(p->status==Status::ok&&p->data==std::vector<unsigned char>(128),"native read");order.push_back(p->handle);}
        }
        check(order.size()==4,"QoS lost/starved transaction");
        check(order.front()==(mode=="priority"?3u:1u),"arbitration order");
        if(mode=="priority")check(order[1]==1,"aged background did not override priority");
        if(mode=="fifo"||mode=="aged")check(order[1]==2,"FIFO/age order");
    }
    std::cout<<"PASS "<<mode<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
