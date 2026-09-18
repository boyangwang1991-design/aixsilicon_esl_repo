#include <aix/esl/timed_channel.hpp>
#include <aix/esl/sparse_store.hpp>
#include <aix/esl/simulation_lifecycle.hpp>
#include <aix/esl/event_recorder.hpp>
#include <aix/esl/traffic_source.hpp>
#include <aix/esl/verification.hpp>
#include <aix/esl/fault_schedule.hpp>
#include <sstream>
#include <iostream>
using namespace aix::esl;using namespace sc_core;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> void rejected(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"expected rejection");}
struct Bench:sc_module{
    std::string mode;bool passed=false;SC_HAS_PROCESS(Bench);
    Bench(sc_module_name n,std::string m):sc_module(n),mode(m){SC_THREAD(run);}
    void run(){
        if(mode=="channel"){
            TimedChannel<int> link(16,1,sc_time(1,SC_NS),sc_time(3,SC_NS),sc_time(2,SC_NS));int value=99;
            check(link.send(7,32)&&!link.send(8,16),"link admission");wait(4,SC_NS);check(!link.receive(value)&&value==99,"early packet");
            wait(1,SC_NS);check(link.receive(value)&&value==7&&!link.send(8,16),"delivery/credit delay");
            wait(2,SC_NS);check(link.send(8,16),"credit return");wait(4,SC_NS);check(link.receive(value)&&value==8,"second packet");
            rejected([&]{link.reset();});wait(2,SC_NS);link.reset();check(link.outstanding()==0,"drained link");
        }else if(mode=="cdc"){
            ClockDomainQueue<int> cdc(2,sc_time(3,SC_NS),2);int value=0;
            wait(1,SC_NS);check(cdc.push(7),"CDC push");wait(7,SC_NS);check(!cdc.pop(value),"CDC early visible");
            wait(1,SC_NS);check(cdc.pop(value)&&value==7,"destination edge visibility");
            cdc.push(8);check(cdc.reset()==1&&!cdc.pop(value),"CDC reset discard");
        }else if(mode=="lifecycle"){
            SimulationLifecycle lifecycle;ProgressWatchdog watchdog(sc_time(3,SC_NS));
            rejected([&]{lifecycle.finish(true);});lifecycle.warmup();wait(2,SC_NS);lifecycle.measure();
            watchdog.waiting("bank0","service",sc_time(12,SC_NS));wait(8,SC_NS);check(!watchdog.stalled(),"long service false positive");
            lifecycle.stop_injection();check(lifecycle.measurement_duration()==sc_time(8,SC_NS)&&!lifecycle.injecting(),"measurement window");
            rejected([&]{lifecycle.finish(false);});wait(5,SC_NS);check(watchdog.stalled()&&watchdog.snapshot().size()==1,"stall diagnosis");
            watchdog.resumed("bank0");check(!watchdog.stalled(),"progress recovery");lifecycle.finish(true);
        }else if(mode=="sparse"){
            SparseStore memory(uint64_t(1)<<40);check(memory.read(100)==0&&memory.allocated_pages()==0,"sparse read allocated");
            memory.write(100,7);memory.write((uint64_t(1)<<40)-1,8);
            check(memory.read(100)==7&&memory.read((uint64_t(1)<<40)-1)==8&&memory.allocated_pages()==2,"sparse backing");
            rejected([&]{memory.write(uint64_t(1)<<40,1);});memory.clear();check(memory.read(100)==0,"sparse reset");
        }else if(mode=="events"){
            EventRecorder trace(EventRecorder::Mode::trace,1),off(EventRecorder::Mode::off);
            for(unsigned i=0;i<3;++i){trace.emit(i,0,"source","accept","bank0");off.emit(i,0,"source","accept","bank0");}
            check(trace.counts().at("accept")==3&&trace.dropped()==2&&off.counts().empty(),"trace cap versus counters");
            std::ostringstream stream;trace.write(stream);check(stream.str().find("dropped=2")!=std::string::npos,"truncation metadata");
        }else if(mode=="checkers"){
            MemoryScoreboard scoreboard(32);Transaction write;write.write=true;write.address=3;
            write.data={1,2,3,4};write.mask={0xff,0};scoreboard.write_committed(write);
            Transaction read;read.address=3;read.data={1,0,3,0};scoreboard.check_read(read);
            read.data[2]=9;rejected([&]{scoreboard.check_read(read);});
            ConservationChecker check_transactions(1);check_transactions.accept(1,16);
            rejected([&]{check_transactions.accept(2,16);});rejected([&]{check_transactions.complete(1,15);});
            rejected([&]{check_transactions.finish();});check_transactions.complete(1,16);check_transactions.finish();
            rejected([&]{check_transactions.complete(1,16);});
            BandwidthChecker bandwidth(sc_time(2,SC_NS),16);bandwidth.transfer(8);bandwidth.transfer(8);
            rejected([&]{bandwidth.transfer(1);});wait(2,SC_NS);bandwidth.transfer(16);
        }else if(mode=="faults"){
            FaultSchedule faults({{"bank0",sc_time(2,SC_NS),sc_time(5,SC_NS),true,false,sc_time(1,SC_NS),50}});
            check(!faults.at("bank0").pause,"early fault");wait(2,SC_NS);
            auto effect=faults.at("bank0");check(effect.pause&&effect.bandwidth_percent==50&&effect.extra_latency==sc_time(1,SC_NS),"fault boundary");
            check(!faults.at("bank1").pause,"fault target isolation");wait(3,SC_NS);check(!faults.at("bank0").pause,"fault recovery");
            rejected([]{FaultSchedule bad({{"bank0",SC_ZERO_TIME,sc_time(2,SC_NS)}, {"bank0",sc_time(1,SC_NS),sc_time(3,SC_NS)}});});
        }else if(mode=="traffic"){
            for(auto pattern:{TrafficSource::Pattern::sequential,TrafficSource::Pattern::stride,TrafficSource::Pattern::random,TrafficSource::Pattern::hotspot}){
                TrafficSource a(128,256,16,pattern,32,50,7,"test"),b(128,256,16,pattern,32,50,7,"test");
                unsigned writes=0;for(unsigned i=0;i<1000;++i){auto x=a.next(),y=b.next();
                    check(x.address==y.address&&x.write==y.write&&x.metadata.id==i&&x.address>=128&&x.address<384&&x.address%16==0,"traffic replay/range");writes+=x.write;}
                check(writes>400&&writes<600,"mixed traffic ratio");}
            rejected([]{TrafficSource bad(UINT64_MAX,32,16,TrafficSource::Pattern::sequential,16,0,0,"bad");});
        }else throw std::invalid_argument("unknown infrastructure test");
        passed=true;sc_stop();
    }
};
int sc_main(int argc,char** argv){try{if(argc!=2)return 2;Bench bench("bench",argv[1]);sc_start();check(bench.passed,"unfinished");return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
