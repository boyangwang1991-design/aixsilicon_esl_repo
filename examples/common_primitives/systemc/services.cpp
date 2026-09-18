#include <aix/esl/register_bank.hpp>
#include <aix/esl/secded.hpp>
#include <aix/esl/task_graph.hpp>
#include <systemc>
#include <iostream>
using namespace aix::esl;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void rejected(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"expected rejection");}
int sc_main(int argc,char** argv){try{
    if(argc!=2)return 2;std::string mode=argv[1];
    if(mode=="registers"){
        RegisterBank regs;unsigned callbacks=0;
        regs.define(0,{0x80000100,0xff,0x100,0xffffffff},[&](uint32_t before,uint32_t after){check(before!=after,"callback values");++callbacks;});
        regs.write(0,0x1a5,1);check(regs.read(0)==0x800001a5,"byte lanes/RW/RO");
        regs.write(0,0x100,2);check(regs.read(0)==0x800000a5&&callbacks==2,"W1C");
        regs.update(0,0x100);check(regs.dump().at(0)==0x800001a5,"hardware update/dump");
        regs.reset();check(regs.read(0)==0x80000100&&callbacks==2,"reset no bus callback");
        rejected([&]{regs.define(0,{});});rejected([&]{regs.write(2,0);});
        InterruptState irq(2);irq.raise(3);check(!irq.asserted(),"masked pending");irq.mask(3);check(irq.asserted(),"coalescing threshold");irq.clear(1);check(!irq.asserted()&&irq.pending()==2,"W1C pending");
    }else if(mode=="ecc"){
        const uint64_t original=0x123456789abcdef0ULL;auto clean=Secded64::encode(original);
        check(Secded64::decode(clean).status==Secded64::Status::clean,"clean ECC");
        auto flip=[](Secded64::Word& w,unsigned bit){if(bit<64)w.data^=uint64_t(1)<<bit;else w.check^=1u<<(bit-64);};
        for(unsigned bit=0;bit<72;++bit){auto w=clean;flip(w,bit);auto r=Secded64::decode(w);
            check(r.status==Secded64::Status::corrected&&r.data==original,"single-bit correction");
            for(unsigned second=bit+1;second<72;++second){auto d=w;flip(d,second);
                check(Secded64::decode(d).status==Secded64::Status::uncorrectable,"double-bit detection");}}
    }else if(mode=="dag"){
        TaskGraph graph({{1,{},4},{2,{1},4},{3,{1},4},{4,{2,3},0}},12,2);
        check(graph.start(1),"load");graph.complete(1,true);check(graph.bytes_used()==4,"retain producer output");
        check(graph.start(2)&&graph.start(3),"parallel consumers");graph.complete(2,true);check(graph.bytes_used()==12,"last consumer owns input");
        graph.complete(3,true);check(graph.bytes_used()==8,"input released at last consumer");
        check(graph.start(4),"store dependency");graph.complete(4,true);check(graph.finished()&&graph.bytes_used()==0,"DAG drain");
        rejected([&]{graph.complete(4,true);});
        TaskGraph failure({{1,{},4},{2,{1},4},{3,{2},0}},8,1);failure.start(1);failure.complete(1,false);
        check(failure.finished()&&failure.bytes_used()==0&&failure.state(3)==TaskGraph::State::cancelled,"failure propagation");
        TaskGraph tight({{1,{},4},{2,{1},4}},4,1);tight.start(1);tight.complete(1,true);check(tight.stalled(),"buffer deadlock detection");
        rejected([]{TaskGraph cycle({{1,{2},0},{2,{1},0}},0,1);});
    }else return 2;
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
