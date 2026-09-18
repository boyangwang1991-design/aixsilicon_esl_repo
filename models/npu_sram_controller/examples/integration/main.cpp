#include <npu_sram_controller/model.hpp>
#include <iostream>
using namespace aix::esl::npu_sram_controller;
int sc_main(int,char**){
    Config a;a.full_data=1;Config b=a;b.mapping="xor";b.stripe_bytes=64;
    Model first("first",a),second("second",b);
    auto step=[](){sc_core::sc_start(1,sc_core::SC_NS);sc_core::sc_start(sc_core::SC_ZERO_TIME);};
    auto read=[&](Model& m,bool wr){Response r;for(unsigned i=0;i<1000;++i){if(m.pop(0,wr,r))return r;step();}throw std::runtime_error("consumer timeout");};
    if(!first.submit({0,0,128,1,0,0,true})||!first.push_w(0,std::vector<uint8_t>(128,42),std::vector<uint8_t>(128,1),true))return 1;
    if(read(first,true).error)return 1;
    if(!first.submit({0,0,128,1,0,first.cycle(),false})||!second.submit({0,0,128,1,0,second.cycle(),false}))return 1;
    auto r=read(first,false),s=read(second,false);for(auto x:r.data)if(x!=42)return 1;for(auto x:s.data)if(x!=0)return 1;
    first.stop_scrub();second.stop_scrub();for(unsigned i=0;i<1000&&(!first.idle()||!second.idle());++i)step();
    if(!first.idle()||!second.idle())return 1;
    first.reset();second.reset();std::cout<<"PASS independent consumer, two instances, copy ownership, drain/reset\n";return 0;
}
