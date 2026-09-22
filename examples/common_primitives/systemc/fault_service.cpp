#include <aix/esl/fault_service.hpp>
#include <aix/esl/byte_store.hpp>
#include <aix/esl/event_recorder.hpp>
#include <aix/esl/verification.hpp>
#include <iostream>
#include <optional>
#include <set>
using namespace aix::esl;
using namespace sc_core;
static void check(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
template<class Error, class F> static void rejected(F action) {
    bool caught = false; try { action(); } catch (const Error&) { caught = true; }
    check(caught, "expected fault service rejection");
}
struct Bench : sc_module {
    std::string mode;
    bool observe, done = false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name name, std::string m, bool tracing) : sc_module(name), mode(m), observe(tracing) { SC_THREAD(run); }
    void run() {
        if (mode == "seeded") {
            const auto tick = [](uint64_t value) { return sc_time::from_value(value); };
            auto schedule = FaultSchedule::seeded("bank0", 4, tick(8), tick(2), 7, "fault.bank0", {true});
            auto replay = FaultSchedule(schedule.windows());
            auto other = FaultSchedule::seeded("bank0", 4, tick(8), tick(2), 7, "other", {true});
            const uint64_t starts[] = {5,9,20,26};
            bool differs = false;
            for (unsigned i = 0; i < 4; ++i) {
                check(schedule.windows()[i].begin.value() == starts[i] && schedule.windows()[i].end.value() == starts[i]+2, "seeded independent integer oracle");
                differs |= schedule.windows()[i].begin != other.windows()[i].begin;
            }
            check(differs, "named random streams not isolated");
            FaultService service(tick(1), tick(1), 1, replay, "bank0");
            std::optional<FaultService::Ticket> flight;
            unsigned accepted = 0;
            for (unsigned time = 0; time < 32; ++time) {
                if (flight) { service.retire(flight->id); flight.reset(); }
                bool paused = false;
                for (auto start : starts) paused |= time >= start && time < start + 2;
                for (unsigned query = 0; query < 3; ++query)
                    check(schedule.at("bank0").pause == paused && replay.at("bank0").pause == paused &&
                          !schedule.at("bank1").pause, "query mutated replay or leaked target");
                flight = service.reserve();
                check(bool(flight) == !paused, "seeded windows were not applied");
                accepted += bool(flight);
                wait(tick(1));
            }
            if (flight) service.retire(flight->id);
            check(accepted == 24 && !service.outstanding(), "seeded service lost credits");
        } else if (mode == "overflow") {
            const auto max = sc_time::from_value(UINT64_MAX);
            rejected<std::invalid_argument>([] { FaultSchedule::seeded("b",1,SC_ZERO_TIME,sc_time(1,SC_NS),0,"s",{}); });
            rejected<std::invalid_argument>([] { FaultSchedule::seeded("b",1,sc_time(1,SC_NS),sc_time(2,SC_NS),0,"s",{}); });
            rejected<std::overflow_error>([&] { FaultSchedule::seeded("b",2,max,sc_time::from_value(1),0,"s",{}); });
            FaultService rate(SC_ZERO_TIME,max,1,FaultSchedule({{"b",SC_ZERO_TIME,max,false,false,SC_ZERO_TIME,50}}),"b");
            rejected<std::overflow_error>([&] { rate.reserve(); }); check(!rate.outstanding(), "rate overflow leaked ticket");
            FaultService delay(sc_time::from_value(1),sc_time::from_value(1),1,
                               FaultSchedule({{"b",SC_ZERO_TIME,max,false,false,max}}),"b");
            rejected<std::overflow_error>([&] { delay.reserve(); }); check(!delay.outstanding(), "delay overflow leaked ticket");
        } else if (mode == "retirement") {
            FaultService service(sc_time(2,SC_NS),sc_time(1,SC_NS),1,
                FaultSchedule({{"bank0",SC_ZERO_TIME,sc_time(3,SC_NS),false,false,sc_time(3,SC_NS)}}),"bank0");
            auto ticket = service.reserve(); check(ticket && ticket->ready == sc_time(5,SC_NS), "extra service latency");
            wait(2,SC_NS);
            rejected<std::logic_error>([&] { service.retire(ticket->id); });
            check(service.outstanding() == 1, "early retirement lost delayed ticket");
            wait(3,SC_NS);
            check(!service.reserve(), "ready but unconsumed credit was released");
            rejected<std::logic_error>([&] { service.reset(); });
            wait(3,SC_NS); service.retire(ticket->id);
            rejected<std::logic_error>([&] { service.retire(ticket->id); });
            service.reset(); auto next = service.reserve();
            check(next && next->id != ticket->id && next->ready == sc_time(10,SC_NS), "recovery/reset ticket identity");
            wait(2,SC_NS); service.retire(next->id);
            // ceil(1 tick * 100 / 33) = 4 ticks, including a window that ends
            // before the already-reserved spacing expires.
            const auto origin = sc_time_stamp(), tick = sc_time::from_value(1);
            FaultService fractional(tick,tick,1,
                FaultSchedule({{"fractional",origin,origin+tick*2,false,false,SC_ZERO_TIME,33}}),"fractional");
            auto first = fractional.reserve(); check(bool(first), "fractional first admission");
            wait(tick); fractional.retire(first->id); wait(tick*2);
            check(!fractional.reserve(), "fractional II rounded down or erased at window end");
            wait(tick); auto recovered = fractional.reserve();
            check(recovered && recovered->ready == origin+tick*5, "fractional rate recovery");
            wait(tick); fractional.retire(recovered->id);
        } else {
            std::vector<FaultSchedule::Window> windows;
            std::vector<unsigned> expected_starts;
            std::set<unsigned> expected_errors;
            unsigned expected_finish;
            if (mode == "baseline") { expected_starts={0,1,2,3,4,5,6,7}; expected_finish=9; }
            else if (mode == "pause" || mode == "zero") {
                windows.push_back({"bank0",SC_ZERO_TIME,sc_time(3,SC_NS),mode=="pause",false,SC_ZERO_TIME,mode=="zero"?0u:100u});
                expected_starts={3,4,5,6,7,8,9,10}; expected_finish=12;
            } else if (mode == "error") {
                windows.push_back({"bank0",sc_time(1,SC_NS),sc_time(4,SC_NS),false,true});
                expected_starts={0,1,2,3,4,5,6,7}; expected_finish=9; expected_errors={1,2,3};
            } else if (mode == "delay") {
                windows.push_back({"bank0",SC_ZERO_TIME,sc_time(3,SC_NS),false,false,sc_time(4,SC_NS)});
                expected_starts={0,1,6,7,8,9,10,11}; expected_finish=13;
            } else if (mode == "bandwidth") {
                windows.push_back({"bank0",SC_ZERO_TIME,sc_time(5,SC_NS),false,false,SC_ZERO_TIME,50});
                expected_starts={0,2,4,6,7,8,9,10}; expected_finish=12;
            } else if (mode == "combined") {
                windows.push_back({"bank0",SC_ZERO_TIME,sc_time(3,SC_NS),false,true,sc_time(4,SC_NS),50});
                expected_starts={0,2,6,8,9,10,11,12}; expected_finish=14; expected_errors={0,1};
            } else throw std::invalid_argument("unknown fault fixture");
            FaultSchedule schedule(windows);
            FaultService service(sc_time(2,SC_NS),sc_time(1,SC_NS),2,schedule,"bank0");
            // An unaffected target sharing the same schedule must retain baseline timing.
            FaultService isolated(sc_time(1,SC_NS),sc_time(1,SC_NS),1,schedule,"bank1");
            auto separate = isolated.reserve(); check(separate && separate->ready == sc_time(1,SC_NS) && !separate->error, "target isolation");
            struct Flight {unsigned id; FaultService::Ticket ticket;};
            std::vector<Flight> flights;
            std::vector<unsigned> starts;
            std::set<unsigned> failures;
            ByteStore memory(64);
            ConservationChecker conservation(2);
            EventRecorder events(observe ? EventRecorder::Mode::trace : EventRecorder::Mode::off);
            unsigned accepted=0, completed=0;
            for (unsigned cycle=0; cycle<80; ++cycle) {
                if (cycle==1) isolated.retire(separate->id);
                for (auto it=flights.begin(); it!=flights.end();) {
                    if (it->ticket.ready > sc_time_stamp()) { ++it; continue; }
                    if (it->ticket.error) failures.insert(it->id);
                    else {
                        const unsigned char value[4] = {static_cast<unsigned char>(it->id+1),2,3,4};
                        check(memory.write(it->id*4,value,4), "successful completion write");
                    }
                    service.retire(it->ticket.id); conservation.complete(it->id,4); ++completed;
                    events.emit(it->id,0,"source",it->ticket.error?"error":"complete","bank0",4);
                    it=flights.erase(it);
                }
                if (accepted<8) if (auto ticket=service.reserve()) {
                    conservation.accept(accepted,4); flights.push_back({accepted,*ticket});
                    events.emit(accepted,0,"source","accept","bank0",4); ++accepted; starts.push_back(cycle);
                }
                check(accepted-completed==service.outstanding() && flights.size()==service.outstanding(), "fault credit conservation");
                if (completed==8) {
                    check(cycle==expected_finish && starts==expected_starts && failures==expected_errors, "fault timing/error oracle");
                    for (unsigned address=0; address<64; ++address) {
                        const unsigned id=address/4, lane=address%4;
                        const unsigned char expected= id>=8 || expected_errors.count(id) ? 0 : lane ? lane+1 : id+1;
                        check(memory[address]==expected, "failed request wrote memory or successful request was lost");
                    }
                    conservation.finish(); check(!isolated.outstanding() && !service.outstanding(), "fault drain");
                    if (observe) check(events.counts().at("accept")==8, "observation count");
                    else check(events.counts().empty(), "off recorder");
                    done=true; sc_stop(); return;
                }
                wait(1,SC_NS);
            }
            check(false,"fault service watchdog");
        }
        done=true; sc_stop();
    }
};
int sc_main(int argc,char**argv) {
    try { check(argc==2||argc==3,"usage: fault_service MODE [off]");
          Bench bench("bench",argv[1],argc==2); sc_start(); check(bench.done,"unfinished"); return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
