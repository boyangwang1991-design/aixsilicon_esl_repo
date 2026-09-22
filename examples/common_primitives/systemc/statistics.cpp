#include <aix/esl/statistics.hpp>
#include <iostream>
using namespace aix::esl;
using namespace sc_core;
static void check(bool valid, const char* why) { if (!valid) throw std::runtime_error(why); }
struct Bench : sc_module {
    std::string mode;
    bool done = false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name name, std::string m) : sc_module(name), mode(m) { SC_THREAD(run); }
    void run() {
        if (mode == "overflow") {
            const auto max = std::numeric_limits<std::uint64_t>::max();
            Counter counter; counter.add(max); counter.add();
            check(counter.snapshot().value == max && counter.snapshot().overflow, "counter wrap");
            Gauge gauge(max);
            wait(sc_time::from_value(2));
            check(gauge.snapshot().integral_ticks == max && gauge.snapshot().overflow && gauge.snapshot().busy_ticks == 2, "gauge multiply wrap");
            gauge.set(1);
            wait(sc_time::from_value(max / 2));
            Span span; span.record(SC_ZERO_TIME, sc_time_stamp()); span.record(SC_ZERO_TIME, sc_time_stamp());
            check(span.snapshot().total_ticks == max && span.snapshot().overflow && span.snapshot().count == 2, "span sum wrap");
        } else {
            check(mode == "on" || mode == "off", "unknown statistics fixture");
            const bool enabled = mode == "on";
            Counter counter(enabled); Gauge gauge(1, enabled); Span span(enabled);
            check(gauge.snapshot().elapsed_ticks == 0 && span.snapshot().count == 0, "zero window/empty span");
            counter.add(4); counter.add(2);
            wait(2, SC_NS); gauge.set(2);
            wait(3, SC_NS); gauge.set(1);
            span.record(SC_ZERO_TIME, sc_time_stamp());
            wait(4, SC_NS); gauge.set(0);
            span.record(sc_time(2, SC_NS), sc_time_stamp());
            const auto before = sc_time_stamp();
            const auto g = gauge.snapshot(), again = gauge.snapshot();
            const auto s = span.snapshot();
            if (enabled) {
                check(counter.snapshot().value == 6 && g.integral_ticks == sc_time(12, SC_NS).value() &&
                      g.busy_ticks == sc_time(9, SC_NS).value() && g.minimum == 0 && g.maximum == 2, "gauge/counter oracle");
                check(s.count == 2 && s.total_ticks == sc_time(12, SC_NS).value() &&
                      s.minimum_ticks == sc_time(5, SC_NS).value() && s.maximum_ticks == sc_time(7, SC_NS).value(), "overlapping spans are not a union");
            } else check(!g.enabled && !s.enabled && !counter.snapshot().enabled &&
                         g.integral_ticks == 0 && g.level == 0 && s.count == 0 && counter.snapshot().value == 0, "off statistics");
            check(sc_time_stamp() == before && again.integral_ticks == g.integral_ticks, "snapshot changes state/time");
            for (unsigned i = 0; i < 2; ++i) {
                bool threw = false;
                try { span.record(i ? SC_ZERO_TIME : sc_time(8, SC_NS), i ? sc_time(10, SC_NS) : sc_time(7, SC_NS)); }
                catch (const std::invalid_argument&) { threw = true; }
                check(threw && span.snapshot().count == s.count, "invalid span changed state or depends on mode");
            }
            span.record(sc_time_stamp(), sc_time_stamp());
            if (enabled) check(span.snapshot().count == 3 && span.snapshot().minimum_ticks == 0, "zero-duration sample");
            wait(1, SC_NS);
            if (enabled) check(gauge.snapshot().elapsed_ticks == sc_time(10, SC_NS).value() &&
                               gauge.snapshot().busy_ticks == sc_time(9, SC_NS).value(), "idle tail");
        }
        done = true; sc_stop();
    }
};
int sc_main(int argc, char** argv) {
    try { check(argc == 2, "usage: statistics on|off|overflow"); Bench bench("bench", argv[1]);
          sc_start(); check(bench.done, "incomplete"); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
