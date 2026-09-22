#include <aix/esl/bounded_queue.hpp>
#include <aix/esl/blocking_gate.hpp>
#include <aix/esl/resource_timing.hpp>
#include <aix/esl/statistics.hpp>
#include <algorithm>
#include <iostream>
#include <vector>
using namespace aix::esl;
using namespace sc_core;

static void check(bool okay, const char* message) {
    if (!okay) throw std::runtime_error(message);
}
template<class F> static void rejected(F action) {
    bool threw = false;
    try { action(); } catch (const std::logic_error&) { threw = true; }
    check(threw, "invalid credit release/reset accepted");
}

// One owner composes independent ingress, end-to-end and completion capacities.
// Tickets remain owned when service finishes or the output FIFO is full.
struct Bench : sc_module {
    struct Result { unsigned id, value; };
    struct Flight { unsigned id; ResourceTiming::Ticket ticket; bool queued = false; };
    BoundedQueue<unsigned> input;
    BoundedQueue<Result> output;
    BlockingGate global;
    ResourceTiming service{sc_time(3, SC_NS), sc_time(1, SC_NS), 1, 2};
    std::vector<Flight> flights;
    std::vector<unsigned> starts, finishes;
    unsigned produced = 0, consumed = 0, blocked_output = 0;
    Counter responses;
    Gauge active;
    Span latency;
    bool counters, done = false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name name, unsigned depth, bool observe)
        : sc_module(name), input(2, observe), output(depth, observe), global(3, observe),
          responses(observe), active(0, observe), latency(observe), counters(observe) {
        SC_THREAD(run);
    }
    void conserved() {
        check(input.available() + input.size() == input.capacity(), "ingress capacity");
        check(output.available() + output.size() == output.capacity(), "output capacity");
        check(global.available() + global.outstanding() == global.capacity(), "global capacity");
        check(service.available() + service.outstanding() == service.capacity(), "service capacity");
        check(produced - consumed == global.outstanding(), "end-to-end credit conservation");
        check(input.size() + flights.size() == global.outstanding(), "input/flight ownership");
        check(flights.size() == service.outstanding(), "service ticket ownership");
        check(output.size() == std::count_if(flights.begin(), flights.end(),
              [](const Flight& f) { return f.queued; }), "output aliases owned flights, not extra tickets");
        if (counters) {
            const auto in = input.stats(), out = output.stats(), gate = global.stats();
            check(in.accepted == in.removed + in.discarded + input.size(), "input counter conservation");
            check(out.accepted == out.removed + out.discarded + output.size(), "output counter conservation");
            check(gate.accepted == gate.removed + global.outstanding(), "gate counter conservation");
        }
    }
    void run() {
        // Admission policy is distinct from the free-capacity query.
        global.drain();
        check(global.available() == 3 && !global.enter(), "closed gate admitted free credit");
        check(global.resume(), "idle gate failed to resume");
        for (unsigned cycle = 0; cycle < 40; ++cycle) {
            if (produced < 6 && !input.full() && global.enter()) {
                check(input.try_push(produced), "input admission");
                ++produced;
            }
            conserved();
            // Deliberate downstream pause, then one response every two cycles.
            if (cycle >= 8 && cycle % 2 == 0) {
                Result value{};
                if (output.try_pop(value)) {
                    check(value.id == consumed && value.value == consumed * 17 + 3, "lost/duplicate/corrupt result");
                    auto it = std::find_if(flights.begin(), flights.end(),
                        [&](const Flight& f) { return f.id == value.id; });
                    check(it != flights.end() && it->queued, "result without owned ticket");
                    const auto ticket = it->ticket.id;
                    latency.record(it->ticket.ready - sc_time(3, SC_NS), sc_time_stamp());
                    responses.add();
                    service.retire(ticket);
                    active.set(service.outstanding());
                    flights.erase(it);
                    global.leave();
                    ++consumed; finishes.push_back(cycle);
                    rejected([&] { service.retire(ticket); });
                }
            }
            conserved();
            for (auto& flight : flights) if (!flight.queued && flight.ticket.ready <= sc_time_stamp()) {
                if (output.try_push({flight.id, flight.id * 17 + 3})) flight.queued = true;
                else ++blocked_output;
            }
            conserved();
            if (!input.empty()) if (auto ticket = service.reserve()) {
                const auto id = input.front();
                check(input.pop(), "lost ingress");
                flights.push_back({id, *ticket, false});
                active.set(service.outstanding());
                starts.push_back(cycle);
                if (cycle == 0) {
                    check(service.available() == 1 && !service.reserve(), "II ignored with free capacity");
                    rejected([&] { service.retire(ticket->id); });
                }
            }
            conserved();
            if (cycle == 4) {
                check(service.available() == 0 && !service.reserve(), "finished outputs released credit early");
                rejected([&] { service.reset(); });
                check(service.outstanding() == 2, "rejected reset changed ownership");
            }
            if (consumed == 6) {
                check(starts == std::vector<unsigned>({0, 1, 8, 10, 12, 14}), "latency/II/backpressure admission schedule");
                check(finishes == std::vector<unsigned>({8, 10, 12, 14, 16, 18}), "consumer completion schedule");
                check((output.capacity() == 1) == (blocked_output > 0), "output capacity did not control staging");
                check(global.idle() && service.outstanding() == 0 && input.empty() && output.empty(), "incomplete drain");
                global.drain(); check(global.resume(), "drained gate did not resume");
                service.reset();
                rejected([&] { global.leave(); });
                if (counters) {
                    check(responses.snapshot().value == 6 && latency.snapshot().count == 6 &&
                          latency.snapshot().total_ticks == sc_time(33, SC_NS).value() &&
                          active.snapshot().integral_ticks == sc_time(33, SC_NS).value() &&
                          active.snapshot().busy_ticks == sc_time(18, SC_NS).value(), "composed statistics oracle");
                    check(global.stats().high_watermark == 3 && output.stats().high_watermark == output.capacity(), "watermarks");
                } else check(!global.stats().enabled && !input.stats().enabled && !output.stats().enabled, "disabled counters");
                done = true; sc_stop(); return;
            }
            wait(1, SC_NS);
        }
        check(false, "credit pipeline failed to drain");
    }
};
int sc_main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: credit_pipeline OUTPUT_DEPTH on|off");
        const std::string depth = argv[1], mode = argv[2];
        check((depth == "1" || depth == "2") && (mode == "on" || mode == "off"), "invalid fixture mode");
        Bench bench("bench", depth == "1" ? 1 : 2, mode == "on");
        sc_start(); check(bench.done, "incomplete simulation");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
