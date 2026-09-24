#include <npu_mesh/model.hpp>
#include <iostream>
using namespace aix::esl::npu_mesh;
int sc_main(int,char**){Config cfg;cfg.columns=2;cfg.rows=1;cfg.endpoints={{1,0,65536,32,3,4}};
    cfg.endpoints[0].backend="sram";cfg.fragment_window=4;
    Model a("a",cfg),b("b",cfg);Request r;r.source=0;r.op=Op::write;r.address=21;r.length=7;r.data={1,2,3,4,5,6,7};
    auto s=a.submit(r);Completion c;bool complete=false;
    for(unsigned i=0;i<1000&&!complete;++i){sc_core::sc_start(1,sc_core::SC_NS);complete=a.take(s.handle,c);}
    if(!complete||c.status!=Status::ok)return 1;
    for(unsigned i=0;i<1000&&!a.idle();++i)sc_core::sc_start(1,sc_core::SC_NS);
    if(!a.idle()||a.inspect(21,7)!=r.data||b.inspect(21,7)!=std::vector<unsigned char>(7,0))return 2;
    a.drain();if(a.submit(r).status!=Status::retry||!a.resume())return 3;
    std::cout<<"PASS independent consumer and instance isolation\n";return 0;
}
