#include <aix/esl/burst_schedule.hpp>
#include <aix/esl/traffic_source.hpp>
#include <aix/esl/workload_trace.hpp>
#include <aix/esl/resource_timing.hpp>
#include <aix/esl/verification.hpp>
#include <systemc>
#include <iostream>
#include <optional>
#include <sstream>
using namespace aix::esl;
using namespace sc_core;
static void check(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
template<class Error, class F> static void rejected(F action) {
    bool caught = false;
    try { action(); } catch (const Error&) { caught = true; }
    check(caught, "expected burst rejection");
}
struct Bench : sc_module {
    std::string mode;
    bool done = false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name name, std::string m) : sc_module(name), mode(m) { SC_THREAD(run); }
    void run() {
        if (mode == "schedule") {
            BurstSchedule first(5, 2, 5, 2), second(5, 2, 5, 4), legacy(5, 1, 0);
            const uint64_t expected[] = {2, 2, 7, 7, 12};
            for (unsigned i = 0; i < 5; ++i)
                check(first.earliest(i) == expected[i] && second.earliest(i) == expected[i] + 2 && legacy.earliest(i) == 0, "phase/burst/tail schedule");
            check(first.size() == 5 && first.total_bytes(4) == 20, "finite count/bytes");
            BurstSchedule single(1, 100, UINT64_MAX, UINT64_MAX);
            check(single.earliest(0) == UINT64_MAX, "single batch wide phase");
            BurstSchedule boundary(2, 1, UINT64_MAX);
            check(boundary.earliest(1) == UINT64_MAX, "last release boundary");
        } else if (mode == "errors") {
            rejected<std::invalid_argument>([] { BurstSchedule bad(0, 1, 1); });
            rejected<std::invalid_argument>([] { BurstSchedule bad(1, 0, 1); });
            rejected<std::overflow_error>([] { BurstSchedule bad(3, 1, UINT64_MAX); });
            rejected<std::overflow_error>([] { BurstSchedule bad(2, 1, 1, UINT64_MAX); });
            BurstSchedule valid(5, 2, 5);
            rejected<std::out_of_range>([&] { valid.earliest(5); });
            rejected<std::invalid_argument>([&] { valid.total_bytes(0); });
            rejected<std::overflow_error>([&] { valid.total_bytes(UINT64_MAX); });
            rejected<std::invalid_argument>([] { TrafficSource bad(0, 32, 4, static_cast<TrafficSource::Pattern>(99), 4, 0, 1, "bad"); });
        } else {
            check(mode == "closed_loop" || mode == "random_replay", "unknown burst fixture");
            BurstSchedule schedule(5, 2, 5, 2);
            auto pattern = mode == "closed_loop" ? TrafficSource::Pattern::sequential : TrafficSource::Pattern::random;
            TrafficSource source(0, 32, 4, pattern, 4, 50, 17, "burst");
            TrafficSource reference(0, 32, 4, pattern, 4, 50, 17, "burst");
            std::vector<WorkloadRequest> expected, recorded;
            for (unsigned i = 0; i < 5; ++i) expected.push_back({reference.next(), schedule.earliest(i), {}});
            ResourceTiming server(sc_time(3, SC_NS), sc_time(1, SC_NS), 1, 1);
            ConservationChecker conservation(1);
            std::optional<Transaction> pending;
            std::optional<ResourceTiming::Ticket> active;
            unsigned accepted = 0, completed = 0, retries = 0, active_id = 0;
            std::vector<unsigned> starts, finishes;
            for (unsigned cycle = 0; cycle < 40; ++cycle) {
                if (active && active->ready <= sc_time_stamp()) {
                    server.retire(active->id); active.reset(); conservation.complete(active_id, 4);
                    ++completed; finishes.push_back(cycle);
                }
                if (accepted < schedule.size() && schedule.earliest(accepted) <= cycle) {
                    if (!pending) {
                        pending = source.next();
                        recorded.push_back({*pending, schedule.earliest(accepted), {}});
                    }
                    const auto& oracle = expected[accepted].transaction;
                    check(pending->metadata.id == accepted && pending->address == oracle.address &&
                          pending->data == oracle.data && pending->write == oracle.write, "retry regenerated/lost request or RNG state");
                    if (mode == "closed_loop") check(pending->address == accepted * 4, "sequential address oracle");
                    auto ticket = server.reserve();
                    if (ticket) {
                        conservation.accept(pending->metadata.id, pending->data.size());
                        active_id = pending->metadata.id; active = ticket;
                        pending.reset(); ++accepted; starts.push_back(cycle);
                    } else ++retries;
                }
                check(accepted - completed == server.outstanding() && server.outstanding() <= 1, "finite outstanding feedback");
                if (completed == schedule.size()) {
                    check(starts == std::vector<unsigned>({2,5,8,11,14}) &&
                          finishes == std::vector<unsigned>({5,8,11,14,17}) && retries == 7, "closed-loop timing oracle");
                    check(!pending && !active && schedule.total_bytes(4) == 20, "end condition/byte budget");
                    conservation.finish();
                    std::ostringstream a, b;
                    WorkloadTrace::write(a, 1000, expected); WorkloadTrace::write(b, 1000, recorded);
                    check(a.str() == b.str(), "backpressure changed input recording");
                    std::istringstream input(b.str());
                    auto replay = WorkloadTrace::read(input, 1000);
                    std::ostringstream roundtrip; WorkloadTrace::write(roundtrip, 1000, replay);
                    check(roundtrip.str() == a.str(), "replay lost burst phases/data");
                    done = true; sc_stop(); return;
                }
                wait(1, SC_NS);
            }
            check(false, "burst drain watchdog");
        }
        done = true; sc_stop();
    }
};
int sc_main(int argc, char** argv) {
    try { check(argc == 2, "usage: bursts schedule|errors|closed_loop|random_replay");
          Bench bench("bench", argv[1]); sc_start(); check(bench.done, "incomplete burst test"); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
