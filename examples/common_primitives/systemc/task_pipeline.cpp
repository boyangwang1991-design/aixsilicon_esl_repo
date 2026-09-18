#include <aix/esl/task_graph.hpp>
#include <aix/esl/resource_timing.hpp>
#include <aix/esl/compute_timing.hpp>
#include <aix/esl/simulation_lifecycle.hpp>
#include <aix/esl/timed_channel.hpp>
#include <aix/esl/event_recorder.hpp>
#include <iostream>
#include <map>
using namespace aix::esl;using namespace sc_core;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Pipeline:sc_module{
    TaskGraph graph;ResourceTiming compute=ComputeTiming{sc_time(1,SC_NS),4,0,1,1,2}.resource(32);
    TimedChannel<uint64_t> load{16,2,sc_time(1,SC_NS),sc_time(1,SC_NS)};
    TimedChannel<uint64_t> store{16,2,sc_time(1,SC_NS),sc_time(1,SC_NS)};
    SimulationLifecycle lifecycle;EventRecorder events;
    std::map<uint64_t,ResourceTiming::Ticket> running;
    uint64_t finish=0;bool passed=false;bool fail_;
    SC_HAS_PROCESS(Pipeline);
    Pipeline(sc_module_name name,size_t capacity,bool fail):sc_module(name),
        graph({{1,{},16},{2,{1},16},{3,{2},0},{4,{},16},{5,{4},16},{6,{5},0}},capacity,6),fail_(fail){SC_THREAD(run);}
    void run(){
        lifecycle.warmup();lifecycle.measure();
        for(unsigned tick=0;tick<200;++tick){
            uint64_t id;
            while(load.receive(id))graph.complete(id,!(fail_&&id==1));
            while(store.receive(id))graph.complete(id,true);
            for(auto it=running.begin();it!=running.end();){if(it->second.ready>sc_time_stamp()){++it;continue;}
                compute.retire(it->second.id);graph.complete(it->first,true);it=running.erase(it);}
            if(graph.finished()){
                lifecycle.stop_injection();lifecycle.finish(load.outstanding()==0&&store.outstanding()==0&&running.empty());
                check(graph.bytes_used()==0,"pipeline leaked buffers");
                if(fail_)check(graph.state(2)==TaskGraph::State::cancelled&&graph.state(3)==TaskGraph::State::cancelled&&graph.state(6)==TaskGraph::State::succeeded,"failed branch propagation");
                // Loads issue at 0/1 ns and return at 2/3 ns. Compute latency
                // is 8 ns with II=1; stores take 2 ns. Last completion is 13 ns.
                check(sc_time_stamp()==sc_time(13,SC_NS),"analytic pipeline completion");
                passed=true;finish=sc_time_stamp().value();sc_stop();return;}
            auto ready=graph.ready();
            for(auto task:ready){
                // Check task buffer admission before committing any external resource.
                // Each stage has a dedicated single-port channel/resource. At most one
                // issue per stage per tick; admission order is stable task-ID order.
                auto kind=(task-1)%3;
                if(kind==0){if(load.outstanding()==2)continue;
                    // A positive 1 ns serialization interval and one issue per loop
                    // ensure the channel is ready at every simulation tick.
                    if(!graph.start(task))continue;
                    check(load.send(task,16),"load issue contract");break;
                }
                if(kind==1){if(running.size()==2)continue;
                    if(!graph.start(task))continue;
                    auto ticket=compute.reserve();check(bool(ticket),"compute issue contract");running.emplace(task,*ticket);break;
                }
                if(store.outstanding()==2)continue;
                if(!graph.start(task))continue;
                check(store.send(task,16),"store issue contract");break;
            }
            check(!graph.stalled(),"pipeline cannot progress with buffer budget");
            wait(1,SC_NS);
        }
        throw std::runtime_error("task pipeline watchdog");
    }
};
int sc_main(int argc,char** argv){try{if(argc!=3)return 2;Pipeline p("pipeline",std::stoul(argv[1]),std::string(argv[2])=="fail");sc_start();check(p.passed,"pipeline unfinished");std::cout<<"finish_tick="<<p.finish<<'\n';return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
