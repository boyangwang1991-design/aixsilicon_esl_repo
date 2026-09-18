#include "npu_sram_controller/model.hpp"
#include <iostream>
#include <set>
#include <random>
using namespace aix::esl::npu_sram_controller;
#define CHECK(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(0)
static void step(unsigned n=1){while(n--){sc_core::sc_start(1,sc_core::SC_NS);sc_core::sc_start(sc_core::SC_ZERO_TIME);}}
static Response receive(Model& m,unsigned port,bool write){Response r;for(unsigned i=0;i<10000;++i){if(m.pop(port,write,r))return r;step();}throw std::runtime_error(m.snapshot());}
static void drain(Model& m){m.stop_scrub();for(unsigned i=0;i<10000&&!m.idle();++i)step();CHECK(m.idle());}
int sc_main(int argc,char**argv){try{
    std::string test=argc>1?argv[1]:"data";Config c;c.ports=2;c.groups=1;c.banks=4;c.capacity=32768;c.word_bytes=32;c.stripe_bytes=32;c.lanes=4;c.full_data=1;
    if(test=="mapping"){
        for(auto policy:{"modulo","xor","contiguous"})for(auto stripe:{16u,32u,64u,128u,512u})for(auto groupfirst:{0u,1u})for(auto local:{0u,1u}){
            c.mapping=policy;c.stripe_bytes=stripe;c.groups=2;c.group_first=groupfirst;c.local_xor=local;Mapper mapper(c);std::set<uint64_t> positions;
            for(uint64_t a=0;a<c.capacity;++a){auto p=mapper.map(a);CHECK(mapper.inverse(p.bank,p.local)==a);positions.insert(p.bank*(c.capacity/c.banks)+p.local);}CHECK(positions.size()==c.capacity);
        }
        c.mapping="region";c.regions={{0,16384,0,{0,1},"xor",32,0,1},{16384,16384,0,{2,3},"modulo",64,0,0}};Mapper mapper(c);
        for(uint64_t a=0;a<c.capacity;++a){auto p=mapper.map(a);CHECK(mapper.inverse(p.bank,p.local)==a);}std::cout<<"PASS mapping full address bijections\n";return 0;
    }
    if(test=="ecc"){c.ecc_bytes=8;c.ecc_lanes=1;}
    if(test=="hierarchy"){c.groups=2;c.topology="hierarchical";c.remote_bytes=8;}
    if(test=="backpressure"){c.rob_beats=1;c.completion_entries=1;}
    if(test=="pipeline"){c.banks=1;c.bank_ii=3;c.read_latency=8;}
    if(test=="partial_race"){c.ecc_bytes=8;c.ecc_lanes=1;}
    if(test=="dual"){c.banks=1;c.bank_entries=32;c.completion_entries=16;}
    if(test=="hol"){c.stripe_bytes=128;c.bank_entries=1;c.completion_entries=1;c.read_latency=32;c.queue="fifo";}
    Model m("dut",c);
    if(test=="hol"){
        auto vconfig=c;vconfig.queue="voq";Model voq("voq",vconfig);
        CHECK(m.submit({0,0,128,1,0,0,false}));CHECK(voq.submit({0,0,128,1,0,0,false}));step();
        CHECK(m.submit({0,1,128,1,128,m.cycle(),false}));CHECK(voq.submit({0,1,128,1,128,voq.cycle(),false}));
        unsigned completed[2]={0,0};uint64_t last[2]={0,0};
        for(unsigned i=0;i<1000&&(completed[0]<2||completed[1]<2);++i){unsigned idx=0;for(Model* x:{&m,&voq}){Response r;if(x->pop(0,false,r)){++completed[idx];last[idx]=r.cycle;}++idx;}step();}
        CHECK(completed[0]==2&&completed[1]==2);CHECK(last[1]<last[0]);CHECK(m.metrics().hol>0);drain(m);drain(voq);std::cout<<"PASS VOQ eliminates HOL "<<last[0]<<" vs "<<last[1]<<"\n";return 0;
    }
    if(test=="aw_w"){
        CHECK(m.push_w(0,std::vector<uint8_t>(8,73),std::vector<uint8_t>(8,1),true));step(12);CHECK(m.metrics().bank_services==0);
        CHECK(m.submit({0,0,8,1,0,m.cycle(),true}));CHECK(!receive(m,0,true).error);
        CHECK(m.submit({0,0,8,1,128,m.cycle(),true}));CHECK(m.submit({0,0,8,1,0,m.cycle(),false}));
        auto r=receive(m,0,false);CHECK(r.data[0]==73);CHECK(m.metrics().completed==2);
        CHECK(m.push_w(0,std::vector<uint8_t>(8,9),std::vector<uint8_t>(8,1),true));CHECK(!receive(m,0,true).error);drain(m);
        std::cout<<"PASS W-before-AW and read independent of missing W\n";return 0;
    }
    if(test=="pipeline"){
        CHECK(m.submit({0,0,32,8,0,0,false}));Response r;for(unsigned i=0;i<8;++i){r=receive(m,0,false);CHECK(r.cycle==16+3*i);}drain(m);
        CHECK(m.metrics().bank_services==8);std::cout<<"PASS independent latency/II pipeline\n";return 0;
    }
    if(test=="partial_race"){
        CHECK(m.submit({0,0,128,1,0,0,true}));CHECK(m.submit({1,0,128,1,0,0,true}));
        std::vector<uint8_t> even(128),odd(128);for(unsigned i=0;i<128;++i){even[i]=i%2==0;odd[i]=i%2==1;}
        CHECK(m.push_w(0,std::vector<uint8_t>(128,17),even,true));CHECK(m.push_w(1,std::vector<uint8_t>(128,91),odd,true));
        CHECK(!receive(m,0,true).error);CHECK(!receive(m,1,true).error);CHECK(m.submit({0,0,128,1,0,m.cycle(),false}));auto r=receive(m,0,false);
        for(unsigned i=0;i<128;++i)CHECK(r.data[i]==(i%2?91:17));drain(m);CHECK(m.metrics().rmw==8);
        m.inject(0,1);CHECK(m.submit({0,0,8,1,0,m.cycle(),false}));CHECK(!receive(m,0,false).error);drain(m);CHECK(m.metrics().corrected==1);
        CHECK(m.submit({0,0,8,1,0,m.cycle(),false}));CHECK(!receive(m,0,false).error);drain(m);CHECK(m.metrics().corrected==1);
        std::cout<<"PASS concurrent RMW and one-time correction repair\n";return 0;
    }
    if(test=="dual"){
        auto fast_config=c;fast_config.dual_port=1;fast_config.dual_ingress=1;Model fast("dual",fast_config);
        for(Model* x:{&m,&fast}){CHECK(x->submit({0,0,32,16,0,0,false}));CHECK(x->submit({1,0,32,16,4096,0,true}));}
        unsigned wr=0,rr[2]={0,0};bool bw[2]={false,false};uint64_t finish[2]={0,0};
        for(unsigned i=0;i<1000&&(!finish[0]||!finish[1]);++i){
            if(wr<16){bool a=m.push_w(1,std::vector<uint8_t>(32,8),std::vector<uint8_t>(32,1),wr==15);bool b=fast.push_w(1,std::vector<uint8_t>(32,8),std::vector<uint8_t>(32,1),wr==15);CHECK(a&&b);++wr;}
            unsigned index=0;for(Model* x:{&m,&fast}){Response r;if(x->pop(0,false,r))++rr[index];if(x->pop(1,true,r))bw[index]=true;
                if(rr[index]==16&&bw[index]&&!finish[index])finish[index]=x->cycle();++index;}step();}
        CHECK(finish[0]&&finish[1]&&finish[1]<finish[0]);drain(m);drain(fast);std::cout<<"PASS dual-port comparison "<<finish[0]<<" vs "<<finish[1]<<"\n";return 0;
    }
    if(test=="negative"){
        bool rejected=false;try{m.submit({0,0,128,2,4096-128,0,false});}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);CHECK(m.metrics().accepted==0);
        auto bad=c;bad.banks=3;rejected=false;try{bad.validate();}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);std::cout<<"PASS negative\n";return 0;
    }
    if(test=="timing"){
        auto id=m.submit({0,0,32,1,0,0,false});CHECK(id);auto r=receive(m,0,false);
        // frontend 1, dispatch 2, fabric hops at 3/4, arrival+bank issue 5,
        // bank completion 7, response submit 7, hops 8/9, delivery 10.
        CHECK(r.cycle==10);drain(m);CHECK(m.metrics().bank_services==1);std::cout<<"PASS exact latency 10\n";return 0;
    }
    if(test=="backpressure"){
        CHECK(m.submit({0,0,128,4,0,0,false}));step(100);CHECK(m.metrics().completed==0);CHECK(m.metrics().bank_services==4);
        for(unsigned i=0;i<4;++i){auto r=receive(m,0,false);CHECK(r.beat==i);CHECK(r.last==(i==3));}drain(m);CHECK(m.metrics().rob_stall);std::cout<<"PASS credit propagation\n";return 0;
    }
    std::vector<uint8_t> oracle(c.capacity,0);std::mt19937 rng(173);
    for(unsigned n=0;n<40;++n){unsigned address=(rng()%128)*128;unsigned port=n%2;std::vector<uint8_t> data(128),mask(128);
        for(unsigned i=0;i<128;++i){data[i]=rng();mask[i]=(n%3)?(rng()%2):1;if(mask[i])oracle[address+i]=data[i];}
        CHECK(m.submit({port,n%c.ids,128,1,address,m.cycle(),true}));step(3);CHECK(m.push_w(port,data,mask,true));auto b=receive(m,port,true);CHECK(!b.error);
        CHECK(m.submit({port,(n+1)%c.ids,128,1,address,m.cycle(),false}));auto r=receive(m,port,false);CHECK(!r.error);
        for(unsigned i=0;i<128;++i)CHECK(r.data[i]==oracle[address+i]);
    }
    drain(m);CHECK(m.metrics().accepted==m.metrics().completed);CHECK(m.metrics().fragments==m.metrics().fragments_done);
    if(test=="ecc"){CHECK(m.metrics().rmw>0);m.inject(0,2);CHECK(m.submit({0,0,8,1,0,m.cycle(),false}));CHECK(receive(m,0,false).error);drain(m);CHECK(m.metrics().uncorrectable);}
    if(test=="ordering"){
        auto a=m.submit({0,0,128,2,0,m.cycle(),false});step();auto b=m.submit({0,0,128,1,8192,m.cycle(),false});CHECK(a&&b);
        CHECK(receive(m,0,false).token==a);CHECK(receive(m,0,false).token==a);CHECK(receive(m,0,false).token==b);drain(m);
    }
    if(test=="reset"){m.reset();CHECK(m.cycle()==0);CHECK(m.submit({0,0,8,1,0,0,false}));bool rejected=false;try{m.reset();}catch(const std::logic_error&){rejected=true;}CHECK(rejected);auto r=receive(m,0,false);for(auto x:r.data)CHECK(x==0);drain(m);}
    std::cout<<"PASS "<<test<<" independent byte oracle\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
