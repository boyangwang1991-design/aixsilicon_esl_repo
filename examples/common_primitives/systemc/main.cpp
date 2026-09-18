#include <aix/esl/bounded_queue.hpp>
#include <aix/esl/blocking_gate.hpp>
#include <aix/esl/byte_store.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
using namespace sc_core;
using namespace aix::esl;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct CopyError {
    int value=0;bool fail=false;
    CopyError(int v,bool f=false):value(v),fail(f){}
    CopyError(const CopyError& other):value(other.value),fail(other.fail){if(fail)throw std::runtime_error("copy injection");}
    CopyError& operator=(const CopyError& other){if(fail)throw std::runtime_error("assignment injection");value=other.value;return *this;}
};
struct Bench:sc_module {
    BoundedQueue<int> queue{2},independent{1};BlockingGate gate{2};
    sc_event start,worker_finished;std::string mode;bool done=false,worker_done=false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name name,std::string m):sc_module(name),mode(m){SC_THREAD(run);SC_THREAD(worker);}
    void worker(){
        if(mode!="queue_events"&&mode!="gate_events"){worker_done=true;return;}
        wait(start);wait(2,SC_NS);
        if(mode=="queue_events"){check(queue.pop(),"worker pop");wait(2,SC_NS);check(queue.try_push(7),"worker push");}
        else gate.leave();
        worker_done=true;worker_finished.notify(SC_ZERO_TIME);
    }
    void run(){
        if(mode=="queue"){
            check(queue.try_push(11)&&queue.try_push(22)&&!queue.try_push(33),"bounded admission");
            int value=99;check(queue.try_pop(value)&&value==11&&queue.try_pop(value)&&value==22,"FIFO/loss");
            check(!queue.try_pop(value)&&value==22&&queue.empty(),"empty pop must preserve output");
            check(independent.try_push(44)&&queue.empty(),"independent queues");
            auto s=queue.stats();check(s.accepted==2&&s.removed==2&&s.rejected==1&&s.high_watermark==2,"queue counters");
        }else if(mode=="queue_clear"){
            queue.try_push(1);queue.try_push(2);queue.clear();queue.clear();
            auto s=queue.stats();check(s.discarded==2&&s.removed==0&&queue.empty(),"clear accounting");
            check(queue.try_push(3)&&queue.front()==3,"reuse after clear");
            queue.pop();try{queue.front();check(false,"empty front accepted");}catch(const std::out_of_range&){}
        }else if(mode=="queue_events"){
            queue.try_push(1);queue.try_push(2);start.notify(SC_ZERO_TIME);
            while(queue.full())wait(queue.changed_event());check(sc_time_stamp()==sc_time(2,SC_NS),"space wake time");
            check(queue.pop(),"pop remaining");while(queue.empty())wait(queue.changed_event());check(queue.front()==7&&sc_time_stamp()==sc_time(4,SC_NS),"data wake time");
        }else if(mode=="queue_copy_error"){
            BoundedQueue<CopyError> q(1);CopyError bad(4,true),good(5);
            try{q.try_push(bad);check(false,"missing copy exception");}catch(const std::runtime_error& e){check(std::string(e.what())=="copy injection","wrong exception");}
            check(q.empty()&&q.stats().accepted==0,"failed copy mutated queue");check(q.try_push(good),"good copy");
            try{q.try_pop(bad);check(false,"missing assignment exception");}catch(const std::runtime_error& e){check(std::string(e.what())=="assignment injection","wrong assignment exception");}
            check(q.size()==1&&q.front().value==5&&q.stats().removed==0,"failed pop dropped record");
            check(!q.try_push(bad),"full push must reject without copying");
        }else if(mode=="gate"){
            check(gate.enter()&&gate.enter()&&!gate.enter(),"capacity");gate.drain();check(!gate.enter()&&!gate.resume(),"closed while outstanding");
            gate.leave();gate.leave();check(gate.idle()&&gate.resume(),"drain/reopen");
            try{gate.leave();check(false,"unbalanced release accepted");}catch(const std::logic_error&){}
            check(gate.outstanding()==0&&gate.enter(),"underflow damaged gate");gate.leave();
        }else if(mode=="gate_exception"){
            static_assert(!std::is_copy_constructible<BlockingLease>::value,"lease must be unique");
            try{BlockingLease a(gate),b(gate),rejected(gate);check(bool(a)&&bool(b)&&!bool(rejected),"lease acquisition");throw std::runtime_error("unwind");}
            catch(const std::runtime_error& e){check(std::string(e.what())=="unwind","lease failure");}
            check(gate.idle(),"exception leaked credit");{BlockingLease a(gate);a.release();a.release();}check(gate.idle(),"double release/destructor");
        }else if(mode=="gate_events"){
            gate.enter();gate.drain();start.notify(SC_ZERO_TIME);while(!gate.idle())wait(gate.idle_event());check(sc_time_stamp()==sc_time(2,SC_NS)&&gate.resume(),"idle event");
        }else if(mode=="store"){
            ByteStore a(8,{1,2,3}),b(8);unsigned char input[]={10,20,30,40},be[]={0xff,0},output[]={99,99,99,99};
            check(a.write(2,input,4,be,2)&&a.read(2,output,4,be,2),"masked transfer");
            check(output[0]==10&&output[1]==99&&output[2]==30&&output[3]==99,"disabled read lanes changed");
            check(a[0]==1&&a[1]==2&&a[2]==10&&a[3]==0&&a[4]==30&&b[2]==0,"storage/instance isolation");
            check(a.write(7,input,1)&&a[7]==10,"last byte");a.clear();for(unsigned i=0;i<8;++i)check(a[i]==0,"clear");
        }else if(mode=="store_errors"){
            ByteStore s(4,{1,2,3,4});unsigned char data[]={8,8,8,8},be[]={0xff,1};
            check(!s.write(0,data,4,be,2)&&s[0]==1,"validate enables before mutation");
            check(!s.write(3,data,2)&&!s.write(std::numeric_limits<std::uint64_t>::max(),data,2),"overflow/range");
            check(!s.read(0,data,0)&&!s.read(0,nullptr,4)&&!s.write(0,nullptr,4),"zero/null");
            check(!s.write(0,data,4,be,0)&&!s.write(0,data,4,nullptr,1),"invalid enables metadata");
            check(s[0]==1&&s[3]==4&&data[0]==8,"failure side effects");
        }else if(mode=="metrics"||mode=="metrics_off"){
            bool on=mode=="metrics";BoundedQueue<int> q(2,on);BlockingGate g(2,on);
            q.try_push(1);g.enter();wait(2,SC_NS);q.try_push(2);g.enter();wait(3,SC_NS);q.pop();g.leave();wait(4,SC_NS);
            auto qs=q.stats(),gs=g.stats();auto before=sc_time_stamp();q.stats();g.stats();check(before==sc_time_stamp(),"observation advanced time");
            if(on){auto ticks=sc_time(12,SC_NS).value();check(qs.occupancy_ticks==ticks&&gs.occupancy_ticks==ticks&&qs.high_watermark==2&&gs.high_watermark==2,"occupancy integral/final interval");check(qs.elapsed_ticks==sc_time(9,SC_NS).value(),"elapsed units");}
            else check(!qs.enabled&&!gs.enabled&&qs.accepted==0&&gs.occupancy_ticks==0,"disabled observation");
            int value=0;check(q.try_pop(value)&&value==2,"observation changed data");g.leave();check(g.idle()&&sc_time_stamp()==sc_time(9,SC_NS),"observation changed end time");
        }else if(mode=="overflow"){
            ActivityMonitor m;m.accepted(std::numeric_limits<std::size_t>::max());wait(sc_time::from_value(2));
            auto s=m.snapshot();check(s.overflow&&s.occupancy_ticks==std::numeric_limits<std::uint64_t>::max(),"integral overflow must saturate");
        }else if(mode=="invalid"){
            try{BoundedQueue<int> q(0);check(false,"zero queue");}catch(const std::invalid_argument&){}
            try{BlockingGate g(0);check(false,"zero gate");}catch(const std::invalid_argument&){}
            try{ByteStore s(0);check(false,"zero store");}catch(const std::invalid_argument&){}
            try{ByteStore s(1,{1,2});check(false,"oversized image");}catch(const std::invalid_argument&){}
        }else throw std::runtime_error("unknown case");
        if(!worker_done&&(mode=="queue_events"||mode=="gate_events"))wait(worker_finished);
        done=true;
    }
};
int sc_main(int argc,char**argv){try{Bench b("bench",argc>1?argv[1]:"queue");sc_start(1,SC_US);check(b.done&&b.worker_done,"watchdog/incomplete");return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
