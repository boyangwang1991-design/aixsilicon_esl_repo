#include <aix/esl/resource_timing.hpp>
#include <aix/esl/address_mapper.hpp>
#include <aix/esl/arbiter.hpp>
#include <aix/esl/transaction.hpp>
#include <aix/esl/ordered_completion.hpp>
#include <aix/esl/deterministic_rng.hpp>
#include <iostream>
#include <set>
using namespace aix::esl;
using namespace sc_core;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void rejected(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"expected rejection");}
struct Bench:sc_module{
    bool passed=false;std::string mode;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name n,std::string m):sc_module(n),mode(m){SC_THREAD(run);}
    void run(){
        if(mode=="pipeline"){
            ResourceTiming pipe(sc_time(8,SC_NS),sc_time(1,SC_NS),1,8);
            std::vector<ResourceTiming::Ticket> tickets;
            for(unsigned i=0;i<8;++i){auto t=pipe.reserve();check(bool(t),"pipelined admission");tickets.push_back(*t);
                check(t->ready==sc_time(i+8,SC_NS),"latency independent of II");check(!pipe.reserve(),"II violation");wait(1,SC_NS);}
            check(!pipe.reserve(),"completed but unconsumed must retain credit");
            pipe.retire(tickets[0].id);check(bool(pipe.reserve()),"credit returned after retirement");
            rejected([&]{pipe.retire(tickets[1].id);});rejected([&]{pipe.retire(tickets[0].id);});
            rejected([&]{pipe.reset();});
        }else if(mode=="resource_instances"){
            ResourceTiming serial(sc_time(3,SC_NS),sc_time(3,SC_NS),2,2);
            auto a=serial.reserve(),b=serial.reserve();check(a&&b&&a->instance!=b->instance&&!serial.reserve(),"parallel servers");
            wait(3,SC_NS);serial.retire(a->id);serial.retire(b->id);serial.reset();check(bool(serial.reserve()),"drained reuse");
            rejected([]{ResourceTiming bad(SC_ZERO_TIME,SC_ZERO_TIME,1,1);});
        }else if(mode=="mapping"){
            for(auto policy:{AddressMapper::Policy::contiguous,AddressMapper::Policy::interleaved,AddressMapper::Policy::xor_interleaved}){
                AddressMapper mapper(4096,8,16,2,policy,1);std::set<uint64_t> locations;
                for(uint64_t a=0;a<4096;++a){auto p=mapper.map(a);check(mapper.inverse(p.bank,p.local)==a,"mapping inverse");
                    check(p.group==p.bank/4&&p.row==p.local/16,"mapping group/row");locations.insert(p.bank*512+p.local);}
                check(locations.size()==4096,"mapping alias");rejected([&]{mapper.map(4096);});rejected([&]{mapper.inverse(8,0);});}
            AddressMapper inter(4096,8,16),xored(4096,8,16,1,AddressMapper::Policy::xor_interleaved);
            std::set<unsigned> hot,spread;
            for(unsigned i=0;i<8;++i){hot.insert(inter.map(i*128).bank);spread.insert(xored.map(i*128).bank);}
            check(hot.size()==1&&spread.size()==8,"stride/XOR analytical distribution");
        }else if(mode=="arbitration"){
            Arbiter rr(3);for(unsigned i=0;i<12;++i)check(rr.grant({true,true,true})==i%3,"RR fairness");
            Arbiter wr(3,Arbiter::Policy::weighted_round_robin,{1,2,3});unsigned counts[3]={};
            for(unsigned i=0;i<60;++i)++counts[*wr.grant({true,true,true})];
            check(counts[0]==10&&counts[1]==20&&counts[2]==30,"weighted shares");
            check(!wr.grant({false,false,false}),"empty ready");rejected([&]{wr.grant({true});});
            Arbiter priority(2,Arbiter::Policy::priority,{},sc_time(3,SC_NS));
            for(unsigned i=0;i<3;++i){check(priority.grant({true,true})==0,"priority");wait(1,SC_NS);}
            check(priority.grant({true,true})==1,"age protection");
        }else if(mode=="transactions"){
            Transaction t;t.address=15;t.data={1,2,3,4};t.mask={0xff,0};t.metadata.id=7;
            tlm::tlm_generic_payload p;TransactionMetadata previous; p.set_extension(&previous);
            {TransactionBinding binding(t,p);check(p.get_data_length()==4&&p.get_address()==15&&p.get_byte_enable_length()==2,"TLM fields");
                check(p.get_extension<TransactionMetadata>()->id==7,"TLM metadata");}
            check(p.get_extension<TransactionMetadata>()==&previous,"extension restoration");p.clear_extension<TransactionMetadata>();
            auto pieces=split_transaction(15,34,16,8);size_t total=0;
            for(auto f:pieces){check(f.offset==total&&f.length<=8&&f.address/16==(f.address+f.length-1)/16,"fragment geometry");total+=f.length;}
            check(total==34,"byte conservation");CompletionAssembly assembly(pieces.size());
            for(size_t i=pieces.size();i>0;--i)assembly.complete(i-1,i!=2);
            check(assembly.ready()&&!assembly.success(),"error assembly");rejected([&]{assembly.complete(0,true);});
            rejected([]{split_transaction(UINT64_MAX,2,16,8);});
            t.mask={1};rejected([&]{t.validate();});
        }else if(mode=="ordering"){
            OrderedCompletion rob(3);check(rob.submit(10,1)&&rob.submit(11,1)&&rob.submit(12,2)&&!rob.submit(13,3),"ROB capacity");
            rob.complete(11,true);rob.complete(12,false);check(!rob.retire(1),"same-ID bypass");
            auto other=rob.retire(2);check(other&&other->tag==12&&!other->success,"independent stream");
            rejected([&]{rob.complete(12,true);});rob.complete(10,true);
            check(rob.retire(1)->tag==10&&rob.retire(1)->tag==11&&rob.barrier_ready(),"ordered retirement/barrier");
        }else if(mode=="random"){
            DeterministicRng a(123,"traffic"),b(123,"traffic"),c(123,"fault");bool differs=false;
            for(unsigned i=0;i<1000;++i){auto x=a.next();check(x==b.next(),"seed replay");differs|=x!=c.next();}
            check(differs,"named streams");auto saved=a.state();auto value=a.next();a.restore(saved);check(a.next()==value,"RNG restore");
            for(unsigned i=0;i<1000;++i)check(a.uniform(7)<7,"bounded random");rejected([&]{a.uniform(0);});
        }else throw std::invalid_argument("unknown architecture test");
        passed=true;sc_stop();
    }
};
int sc_main(int argc,char** argv){try{if(argc!=2)return 2;Bench bench("bench",argv[1]);sc_start();check(bench.passed,"not finished");return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
