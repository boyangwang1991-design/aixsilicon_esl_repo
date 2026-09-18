#include <aix/esl/compute_timing.hpp>
#include <aix/esl/register_bank.hpp>
#include <iostream>
using namespace aix::esl;
using namespace sc_core;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "expected policy rejection");
}
struct ComputeBench : sc_module {
    bool passed = false;
    SC_HAS_PROCESS(ComputeBench);
    ComputeBench(sc_module_name name) : sc_module(name) { SC_THREAD(run); }
    void run() {
        ComputeTiming timing{sc_time(2, SC_NS), 4, 3, 1, 1, 2};
        check(timing.latency(15) == sc_time(14, SC_NS), "ceil work/throughput plus pipeline");
        check(timing.latency(16) == timing.latency(15), "tail work rounding");
        auto resource = timing.resource(15);
        auto first = resource.reserve();
        check(first && first->ready == sc_time(14, SC_NS) && !resource.reserve(), "compute latency/II");
        wait(2, SC_NS);
        auto second = resource.reserve();
        check(second && second->ready == sc_time(16, SC_NS), "pipelined compute issue");
        wait(12, SC_NS);
        check(!resource.reserve(), "completed output still owns capacity");
        resource.retire(first->id);
        wait(2, SC_NS);
        resource.retire(second->id);
        rejects([&]{ timing.latency(0); });
        rejects([&]{ timing.latency(UINT64_MAX); });
        timing.pipeline_cycles = UINT64_MAX;
        rejects([&]{ timing.latency(4); });
        timing.units_per_cycle = 0;
        rejects([&]{ timing.latency(4); });
        passed = true;
        sc_stop();
    }
};
void interrupts() {
    InterruptState irq(1, 1); // bit0 edge, bit1 level
    irq.sample(3); check(irq.pending() == 3 && !irq.asserted(), "masked events must remain pending");
    irq.mask(3); check(irq.asserted(), "pending enable");
    irq.sample(3, 3); check(irq.pending() == 2, "held edge versus held level/W1C");
    irq.sample(0, 2); check(!irq.asserted(), "deassert and clear level");
    irq.sample(1, 1); check(irq.pending() == 1, "new edge dominates simultaneous W1C");
    irq.sample(1, 1); check(!irq.asserted(), "edge is not retriggered by held input");
    irq.reset(); irq.mask(3); irq.sample(1); check(irq.asserted(), "reset clears sampled history");
    InterruptState threshold(2, 3);
    threshold.mask(3); threshold.sample(1); check(!threshold.asserted(), "threshold below crossing");
    threshold.sample(3); check(threshold.asserted(), "coalesced independent rising edges");
    threshold.clear(1); check(!threshold.asserted(), "threshold falling crossing");
}
int sc_main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        if (std::string(argv[1]) == "compute") {
            ComputeBench bench("bench"); sc_start(); check(bench.passed, "compute bench unfinished");
        } else if (std::string(argv[1]) == "interrupts") interrupts();
        else return 2;
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
