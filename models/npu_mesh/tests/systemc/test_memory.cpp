#include <npu_mesh/model.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
using namespace aix::esl::npu_mesh;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void tick(){sc_core::sc_start(1,sc_core::SC_NS);}
static Completion take(Model& m,std::uint64_t h){Completion c;for(unsigned i=0;i<100000;++i){if(m.take(h,c))return c;tick();}throw std::runtime_error("completion watchdog");}
static DmaCompletion take(TensorDma& dma,std::uint64_t h){DmaCompletion c{};for(unsigned i=0;i<100000;++i){if(dma.take(h,c))return c;tick();}throw std::runtime_error("DMA watchdog");}
static void drain(Model& m){for(unsigned i=0;i<100000&&!m.idle();++i)tick();check(m.idle(),"drain watchdog");}
static Request write(unsigned source,std::uint64_t address,unsigned bytes,unsigned char value=0x5a) {
    Request r;r.op=Op::write;r.source=source;r.address=address;r.length=bytes;r.data.assign(bytes,value);return r;
}
static Config config(bool sram=true) {
    Config c;c.columns=4;c.rows=1;c.packet_bytes=256;c.fragment_window=8;c.dma_window=4;
    c.endpoints={{3,0,65536,32,40,16},{1,65536,65536,32,40,16}};
    for(auto& e:c.endpoints) {
        e.backend=sram?"sram":"ddr";e.sram.ports=4;e.sram.banks=4;e.sram.groups=1;
        e.sram.stripe_bytes=128;e.sram.bank_ii=4;e.sram.outstanding=4;
        e.sram.bank_entries=2;e.sram.w_beats=2;e.sram.read_latency=8;e.sram.write_latency=8;
    }
    return c;
}
int sc_main(int argc,char** argv){try {
    std::string scenario=argc>1?argv[1]:"sram_data";
    auto c=config(scenario!="window"&&scenario!="ddr");
    if(scenario=="slow_fanout") {c.endpoints.push_back(c.endpoints[1]);c.endpoints[2].base=131072;c.endpoints[2].router=2;c.endpoints[1].tile=true;c.endpoints[2].tile=true;}
    if(scenario=="reservation")c.return_bytes=4096;
    if(scenario=="timeout_native") {c.packet_bytes=4096;c.timeout_cycles=200;c.endpoints[0].router=0;c.endpoints[0].sram.write_latency=500;c.endpoints[0].sram.w_beats=1;}
    auto other=c;
    if(scenario=="window") {c.fragment_window=1;other.fragment_window=8;c.endpoints[0].latency=100;other.endpoints[0].latency=100;}
    if(scenario=="sram_time")other.endpoints[0].latency=8000;
    if(scenario=="bank")for(auto* cfg:{&c,&other})for(auto& e:cfg->endpoints)e.sram.bank_ii=32;
    if(scenario=="reset_native") {c.packet_bytes=4096;c.endpoints[0].sram.write_latency=100;c.endpoints[0].sram.w_beats=1;}
    if(scenario=="ddr")other.endpoints[0].turnaround_cycles=16;
    Model a("a",c),b("b",other);TensorDma dma("dma",a);
    if(scenario=="sram_data") {
        auto r=write(0,4093,519);
        r.enables.resize(r.length);for(unsigned i=0;i<r.length;++i){r.data[i]=i%251;r.enables[i]=i%3;}
        auto done=take(a,a.submit(r).handle);check(done.status==Status::ok&&done.bytes==519,"SRAM write");
        r.op=Op::read;auto read=take(a,a.submit(r).handle);check(read.status==Status::ok,"SRAM read");
        for(unsigned i=0;i<r.length;++i)check(read.data[i]==(i%3?i%251:0),"SRAM narrow mask mapping");
        drain(a);check(a.inspect(4093,519)==read.data,"debug and real storage diverged");
        check(a.inspect(4092,1)[0]==0&&a.inspect(4612,1)[0]==0,"padding wrote adjacent data");
        std::ostringstream report;a.target_report(0,report);check(report.str().find("\"bank_services\":0")==std::string::npos,"SRAM backend bypassed");
    }else if(scenario=="window"||scenario=="sram_time") {
        auto r=write(0,0,4096);auto x=a.submit(r),y=b.submit(r);
        auto ca=take(a,x.handle),cb=take(b,y.handle);
        check(ca.status==Status::ok&&cb.status==Status::ok,"window transaction");
        if(scenario=="window")check(cb.done<ca.done&&b.metrics().fragment_peak>1,"fragment window has no overlap");
        else check(ca.done==cb.done,"SRAM service charged DDR latency twice");
        drain(a);drain(b);check(a.inspect(0,4096)==b.inspect(0,4096),"window data mismatch");
        std::cout<<"cycles_a="<<ca.done<<" cycles_b="<<cb.done<<'\n';
    }else if(scenario=="bank"||scenario=="ddr") {
        std::vector<std::uint64_t> ah,bh;
        for(unsigned i=0;i<16;++i) {
            auto ra=write(i%4,(scenario=="bank"?512:256)*i,scenario=="bank"?32:256);ra.id=i;
            auto rb=ra;if(scenario=="bank")rb.address=128*i;
            if(scenario=="ddr"&&i%2){ra.op=Op::read;rb.op=Op::read;}
            ah.push_back(a.submit(ra).handle);bh.push_back(b.submit(rb).handle);
        }
        std::uint64_t ta=0,tb=0;
        for(auto h:ah){auto r=take(a,h);check(r.status==Status::ok,"memory a");ta=std::max(ta,r.done);}
        for(auto h:bh){auto r=take(b,h);check(r.status==Status::ok,"memory b");tb=std::max(tb,r.done);}
        if(scenario=="bank")check(ta>tb,"bank conflicts not reflected in latency");
        else check(tb>ta&&ta>=4096/32,"DDR shared bandwidth/turnaround not modeled");
        drain(a);drain(b);std::cout<<"cycles_a="<<ta<<" cycles_b="<<tb<<'\n';
        a.target_report(0,std::cout);std::cout<<'\n';b.target_report(0,std::cout);std::cout<<'\n';
    }else if(scenario=="multicast_large") {
        std::vector<unsigned char> bytes(8192);for(unsigned i=0;i<bytes.size();++i)bytes[i]=i%251;
        a.initialize(0,bytes);DmaDescriptor d;d.segments={{0,65536,8192,0},{0,81920,8192,0},{0,98304,8192,0}};
        auto result=take(dma,dma.submit(d).handle);check(result.status==Status::ok,"large multicast");
        check(dma.source_bytes()==8192&&result.bytes==3*8192,"multicast reread source or lost destination");
        drain(a);for(auto address:{65536,81920,98304})check(a.inspect(address,8192)==bytes,"large multicast data");
        for(unsigned n:result.bytes_per_destination)check(n==8192,"destination completion count");
    }else if(scenario=="slow_fanout") {
        a.initialize(0,std::vector<unsigned char>(8192,0x44));auto t1=a.reserve(0,65536,8192),t2=a.reserve(0,131072,8192);
        DmaDescriptor d;d.segments={{0,65536,8192,t1},{0,131072,8192,t2}};a.stall_target(2,true);
        auto s=dma.submit(d);for(unsigned i=0;i<2000;++i)tick();DmaCompletion result{};
        check(!dma.take(s.handle,result)&&!a.release(t1)&&!a.release(t2),"slow peer caused early completion/reuse");
        a.stall_target(2,false);result=take(dma,s.handle);drain(a);
        check(result.status==Status::ok&&result.bytes==16384&&dma.source_bytes()==8192,"slow fanout replay/loss");
        check(a.inspect(65536,8192)==a.inspect(131072,8192),"slow destination data");
        check(a.release(t1)&&a.release(t2),"DMA pins leaked");
    }else if(scenario=="reservation") {
        Request r;r.length=4096;auto first=a.submit(r);check(first.status==Status::ok,"reserve read");
        r.length=32;check(a.submit(r).status==Status::retry,"read overcommitted return bytes");
        for(unsigned i=0;i<10000&&!a.metrics().completed;++i)tick();
        check(a.submit(r).status==Status::retry,"unconsumed completion released return bytes");
        check(take(a,first.handle).status==Status::ok,"reserved read failed");
        check(take(a,a.submit(r).handle).status==Status::ok&&a.metrics().return_reserved_peak==4096,"reservation not released");
    }else if(scenario=="timeout_native") {
        auto s=a.submit(write(0,0,4096));auto result=take(a,s.handle);drain(a);
        check(result.status==Status::timeout&&result.uncertain&&result.done>500,"native timeout completed before drain");
        check(result.bytes==4096&&a.inspect(0,4096)==std::vector<unsigned char>(4096,0x5a),"timeout write effects lost");
    }else if(scenario=="reset_native") {
        auto s=a.submit(write(0,0,4096));bool native=false;
        for(unsigned i=0;i<10000&&!native;++i){tick();std::ostringstream report;a.target_report(0,report);native=report.str().find("\"accepted\":1")!=std::string::npos;}
        check(native,"native SRAM AW not seen");a.reset();check(!a.resume(),"reset released live SRAM");
        auto result=take(a,s.handle);check(result.status==Status::aborted&&result.uncertain,"reset status");
        drain(a);check(result.bytes==4096&&a.inspect(0,4096)==std::vector<unsigned char>(4096,0x5a),"native W not drained or effect lost");
        check(a.resume(),"resume after native drain");check(take(a,a.submit(write(0,8192,32)).handle).status==Status::ok,"post reset write");
    }else if(scenario=="partial"||scenario=="cancel"||scenario=="dma_reset") {
        a.initialize(0,std::vector<unsigned char>(8192,0x36));
        DmaDescriptor d;d.segments={{0,65536,8192,0},{0,81920,8192,0}};
        auto s=dma.submit(d);bool visible=false;
        for(unsigned i=0;i<10000&&!visible;++i){tick();std::ostringstream out;a.target_report(1,out);auto report=out.str();auto pos=report.find("\"write_bytes\":");visible=pos!=std::string::npos&&std::stoull(report.substr(pos+14))>0;}
        check(visible,"no DMA progress");
        if(scenario=="partial")a.corrupt_next(1);else if(scenario=="cancel")dma.cancel(s.handle);else a.reset();
        auto result=take(dma,s.handle);check(result.status!=Status::ok,"fault/cancel/reset reported success");
        drain(a);unsigned counted=0;
        for(unsigned dest=0;dest<2;++dest){auto bytes=a.inspect(d.segments[dest].destination,8192);unsigned nonzero=std::count(bytes.begin(),bytes.end(),0x36);
            check(nonzero==result.bytes_per_destination[dest],"partial side effects not accounted");counted+=nonzero;}
        check(counted==result.bytes&&counted>0,"aggregate partial count");check(dma.idle(),"DMA did not drain");
    }else throw std::runtime_error("unknown case");
    std::cout<<"PASS "<<scenario<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
