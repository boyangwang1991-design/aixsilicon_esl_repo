#include <npu_mesh/model.hpp>
#include <npu_mesh/axi.hpp>
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace aix::esl::npu_mesh;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void ticks(unsigned n=1){sc_core::sc_start(double(n),sc_core::SC_NS);}
static Completion complete(Model& m,std::uint64_t h){Completion c;for(unsigned i=0;i<20000;++i){if(m.take(h,c))return c;ticks();}throw std::runtime_error("completion watchdog");}
static void drain(Model& m){for(unsigned i=0;i<1000&&!m.idle();++i)ticks();check(m.idle(),"not drained");}
static Request write(unsigned src,std::uint64_t a,unsigned n,unsigned value=0x63){Request r;r.op=Op::write;r.source=src;r.address=a;r.length=n;r.data.assign(n,value);return r;}
int sc_main(int argc,char** argv){try{
    std::string test=argc>1?argv[1]:"data";Config cfg;cfg.columns=2;cfg.rows=2;cfg.endpoints={{3,0,65536,32,8,4,~std::uint64_t(0),false},{1,65536,65536,16,3,4,~std::uint64_t(0),false}};
    cfg.depth=2;cfg.credit_latency=5;cfg.packet_bytes=128;cfg.outstanding=8;
    if(test=="split")cfg.split=true;if(test=="central")cfg.centralized=true;
    if(test=="timeout")cfg.timeout_cycles=50;
    if(test=="timeout_tail"){cfg.timeout_cycles=30;cfg.packet_bytes=1024;}
    if(test=="token")cfg.endpoints[1].tile=true;
    if(test=="config"){
        auto bad=cfg;bad.depth=0;bool rejected=false;try{bad.validate();}catch(std::invalid_argument&){rejected=true;}check(rejected,"zero depth accepted");
        bad=cfg;bad.endpoints[1].base=3;rejected=false;try{bad.validate();}catch(std::invalid_argument&){rejected=true;}check(rejected,"overlap accepted");
    }
    Model m("bus",cfg), independent("independent",cfg);TensorDma dma("dma",m);SyncEvents sync;AxiNiu axi("axi",m,0,0,16,2);
    if(test=="data"||test=="split"||test=="central"||test=="config"){
        auto r=write(0,4093,519);for(unsigned i=0;i<r.length;++i)r.data[i]=i%251;
        r.enables.resize(r.length);for(unsigned i=0;i<r.length;++i)r.enables[i]=i%3!=0;
        auto s=m.submit(r);check(s.status==Status::ok,"write accept");auto c=complete(m,s.handle);check(c.status==Status::ok&&c.bytes==519&&c.visible<c.done,"visibility");
        r.op=Op::read;r.data.clear();r.enables.clear();s=m.submit(r);c=complete(m,s.handle);
        for(unsigned i=0;i<r.length;++i)check(c.data[i]==(i%3?i%251:0),"sparse byte mismatch");
        drain(m);check(independent.inspect(4093,519)==std::vector<unsigned char>(519,0),"instance leakage");
        r.address=UINT64_MAX-3;check(m.submit(r).status==Status::decode,"overflow accepted");check(m.metrics().occupancy_high<=cfg.depth,"FIFO bound");
    }else if(test=="axi"){
        AxiWriteBeat beat;beat.data.assign(16,0);beat.strobes.assign(16,0);check(!axi.w(beat),"W before AW accepted without storage");
        AxiAddress address;address.address=3;address.id=7;address.beats=3;address.beat_bytes=4;check(axi.aw(address),"AW rejected");
        AxiB b;for(unsigned n=0;n<3;++n){ticks(5);check(!axi.b(b),"early B");std::fill(beat.strobes.begin(),beat.strobes.end(),0);
            for(unsigned lane=(n?4*n:3);lane<4*(n+1);++lane){beat.strobes[lane]=lane%2;beat.data[lane]=lane+10;}beat.last=n==2;check(axi.w(beat),"W backpressure");}
        bool got=false;for(unsigned i=0;i<1000&&!got;++i){ticks();got=axi.b(b);}check(got&&b.id==7&&b.status==Status::ok,"B mismatch");
        check(axi.ar(address),"AR reject");unsigned count=0;for(unsigned i=0;i<1000&&count<3;++i){ticks();AxiR r;if(!axi.r(r))continue;check(r.id==7&&r.status==Status::ok&&r.last==(count==2),"R framing");
            for(unsigned lane=count?4*count:3;lane<4*(count+1);++lane)check(r.data[lane]==(lane%2?lane+10:0),"narrow unaligned data");++count;}check(count==3,"R missing beats");
        address.address=0x800000;check(axi.ar(address),"error AR not accepted");count=0;for(unsigned i=0;i<30;++i){ticks();AxiR r;if(axi.r(r)){check(r.status==Status::decode,"wrong decode status");++count;}}check(count==3,"error R framing");
        check(axi.aw(address),"error AW");for(unsigned n=0;n<3;++n){beat.last=n==2;std::fill(beat.strobes.begin(),beat.strobes.end(),0);check(axi.w(beat),"error W drain");}got=false;
        for(unsigned i=0;i<30&&!got;++i){ticks();got=axi.b(b);}check(got&&b.status==Status::decode,"error B");
    }else if(test=="ordering"){
        auto a=m.submit(write(0,0,512,1));auto b=m.submit(write(0,0,512,2));Request f;f.op=Op::fence;auto fence=m.submit(f);
        auto ca=complete(m,a.handle),cb=complete(m,b.handle),cf=complete(m,fence.handle);check(ca.done<cb.done&&cb.done<=cf.done,"order/fence");drain(m);check(m.inspect(0,512)==std::vector<unsigned char>(512,2),"last writer");
        m.corrupt_next(0);auto fail=m.submit(write(0,0,64));check(complete(m,fail.handle).status==Status::corrupt,"corruption ignored");fence=m.submit(f);check(complete(m,fence.handle).status==Status::target,"fence lost failure");
    }else if(test=="backpressure"){
        m.stall_target(0,true);std::vector<std::uint64_t> handles;
        for(unsigned i=0;i<cfg.outstanding;++i){auto r=write(0,i*256,256);r.id=i;auto s=m.submit(r);check(s.status==Status::ok,"capacity early rejection");handles.push_back(s.handle);}
        check(m.submit(write(0,4096,32)).status==Status::retry,"missing admission backpressure");ticks(150);
        auto cold=write(2,65536,64);check(complete(m,m.submit(cold).handle).status==Status::ok,"cold endpoint blocked");
        m.stall_target(0,false);for(auto h:handles)check(complete(m,h).status==Status::ok,"lost pressured transaction");drain(m);check(m.metrics().credit_stalls>0,"no measured stalls");
    }else if(test=="dma"||test=="multicast"){
        std::vector<unsigned char> src(1024);for(unsigned i=0;i<src.size();++i)src[i]=i%251;m.initialize(0,src);
        DmaDescriptor d;if(test=="dma")d=TensorDma::strided(0,0,1,0,65536,4,64,128,96);
        else {d.source=0;d.segments={{0,65536,256,0},{0,66048,256,0},{0,66560,256,0}};}
        auto s=dma.submit(d);check(s.status==Status::ok,"DMA rejected");d.segments[0].source=4000;DmaCompletion c{};
        for(unsigned i=0;i<20000&&!dma.take(s.handle,c);++i)ticks();check(c.handle==s.handle&&c.status==Status::ok,"DMA failed");drain(m);
        if(test=="dma")for(unsigned row=0;row<4;++row)check(m.inspect(65536+96*row,64)==std::vector<unsigned char>(src.begin()+128*row,src.begin()+128*row+64),"stride mismatch");
        else {for(auto a:{65536,66048,66560})check(m.inspect(a,256)==std::vector<unsigned char>(src.begin(),src.begin()+256),"multicast mismatch");check(dma.source_bytes()==256,"source replication reread");}
        DmaDescriptor overlap;overlap.segments={{0,1,16,0}};check(dma.submit(overlap).status==Status::unsupported,"overlap accepted");
        DmaDescriptor empty;auto z=dma.submit(empty);ticks(3);check(dma.take(z.handle,c)&&c.status==Status::ok,"zero length no-op");
    }else if(test=="dma_cancel"){
        m.stall_target(0,true);DmaDescriptor d;d.segments={{0,65536,512,0}};auto s=dma.submit(d);ticks(20);dma.cancel(s.handle);
        DmaCompletion result{};check(!dma.take(s.handle,result),"cancel discarded active read");m.stall_target(0,false);
        for(unsigned i=0;i<20000&&!dma.take(s.handle,result);++i)ticks();check(result.handle==s.handle&&result.status==Status::aborted,"cancel failed");
        drain(m);check(m.inspect(65536,512)==std::vector<unsigned char>(512,0),"cancel issued future write");
    }else if(test=="timeout_tail"){
        auto s=m.submit(write(0,0,1024));check(complete(m,s.handle).status==Status::timeout,"tail timeout missing");drain(m);
        check(m.inspect(0,1024)==std::vector<unsigned char>(1024,0),"expired uncommitted packet wrote memory");
    }else if(test=="management"){
        auto s=m.submit(write(0,0,32));m.drain();check(m.submit(write(0,65536,32)).status==Status::retry,"drain accepted new");
        check(!m.reconfigure_map(cfg.endpoints),"map changed in flight");complete(m,s.handle);drain(m);
        auto mapped=cfg.endpoints;mapped[1].base=131072;check(m.reconfigure_map(mapped),"idle map rejected");check(m.resume(),"resume");
        check(m.submit(write(0,65536,32)).status==Status::decode,"old map retained");check(complete(m,m.submit(write(0,131072,32)).handle).status==Status::ok,"new map unusable");
    }else if(test=="reset"||test=="timeout"){
        m.stall_target(0,true);auto a=m.submit(write(0,0,512));ticks(30);
        if(test=="reset")m.reset();auto c=complete(m,a.handle);check(c.status==(test=="reset"?Status::aborted:Status::timeout),"wrong terminal status");
        check(c.uncertain,"lost partial effect warning");m.stall_target(0,false);drain(m);if(test=="reset")check(m.resume(),"resume failed");
        auto s=m.submit(write(3,65536,16));check(complete(m,s.handle).status==Status::ok,"post recovery failed");
    }else if(test=="token"){
        auto r=write(0,65536,32);check(m.submit(r).status==Status::denied,"unreserved push accepted");r.token=m.reserve(0,65536,64);check(r.token!=0,"reservation");
        auto s=m.submit(r);check(!m.release(r.token),"early reuse");check(complete(m,s.handle).status==Status::ok,"reserved push");
        m.corrupt_next(1);s=m.submit(r);check(complete(m,s.handle).status==Status::corrupt&&m.poisoned(r.token),"buffer not poisoned");check(m.submit(r).status==Status::denied,"poisoned write allowed");
    }else if(test=="command"){
        m.stall_target(0,true);auto s=m.submit(write(0,0,256));check(m.command(0,7)&&m.command(1,9),"command accept");ticks(5);std::uint64_t v;
        check(m.take_command(1,v)&&v==9,"independent command blocked");check(m.take_command(0,v)&&v==7,"command lost");
        check(sync.arrive(0,0,4,1,Status::ok)&&!sync.arrive(0,0,4,1,Status::ok),"dedup failed");check(sync.query(0,0,4,6)==Status::retry,"early barrier");
        sync.arrive(0,0,4,2,Status::ok);check(sync.query(0,0,4,6)==Status::ok,"barrier");sync.reset(1);check(!sync.arrive(0,0,4,0,Status::ok),"late event");m.reset();complete(m,s.handle);
    }else throw std::runtime_error("unknown case");
    std::cout<<"PASS "<<test<<" cycles="<<m.metrics().cycles<<" link_bytes="<<m.metrics().link_bytes<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
