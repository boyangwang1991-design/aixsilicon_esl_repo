#include "config.hpp"
#include <aix/esl/address_mapper.hpp>
#include <aix/esl/arbiter.hpp>
#include <aix/esl/byte_store.hpp>
#include <aix/esl/sparse_store.hpp>
#include <aix/esl/event_recorder.hpp>
#include <aix/esl/ordered_completion.hpp>
#include <aix/esl/resource_timing.hpp>
#include <aix/esl/traffic_source.hpp>
#include <aix/esl/burst_schedule.hpp>
#include <aix/esl/verification.hpp>
#include <aix/esl/simulation_lifecycle.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <variant>
using namespace aix::esl;
using namespace aix::esl::multibank;
using namespace sc_core;
static void require(bool valid, const char* why) { if (!valid) throw std::runtime_error(why); }
using Store = std::variant<ByteStore, SparseStore>;

struct MultiBank : sc_module {
    struct Flight {
        Transaction transaction;
        unsigned bank;
        ResourceTiming::Ticket ticket;
        bool visible = false, returned = false;
        std::uint64_t accepted_cycle;
    };
    Config config;
    AddressMapper mapper;
    OrderedCompletion completions;
    ConservationChecker conservation;
    MemoryScoreboard scoreboard;
    EventRecorder events;
    SimulationLifecycle lifecycle;
    std::vector<std::unique_ptr<ResourceTiming>> resources;
    std::vector<Store> storage;
    std::vector<Arbiter> arbiters;
    std::vector<WorkloadRequest> workload;
    std::vector<std::vector<std::size_t>> ports;
    std::vector<std::size_t> next;
    std::vector<bool> queued;
    std::vector<Flight> flights;
    std::set<std::uint64_t> retired;
    std::uint64_t cycle = 0, accepted = 0, measured = 0, measured_latency = 0;
    std::uint64_t data_hash = 14695981039346656037ULL;
    bool passed = false;
    SC_HAS_PROCESS(MultiBank);
    MultiBank(sc_module_name name, Config c, const std::string& trace) : sc_module(name), config(c),
        mapper(c.capacity, c.banks, c.stripe, 1, c.mapping == "xor" ? AddressMapper::Policy::xor_interleaved :
               c.mapping == "contiguous" ? AddressMapper::Policy::contiguous : AddressMapper::Policy::interleaved),
        completions(c.outstanding), conservation(c.outstanding), scoreboard(c.capacity),
        events(c.observation == "off" ? EventRecorder::Mode::off : c.observation == "trace" ? EventRecorder::Mode::trace : EventRecorder::Mode::counters, c.trace_limit),
        ports(c.ports), next(c.ports, 0), queued(c.ports, false) {
        config.validate();
        for (unsigned bank = 0; bank < c.banks; ++bank) {
            resources.emplace_back(new ResourceTiming(duration(c.latency), duration(c.interval), 1, c.bank_capacity));
            if (c.storage == "dense") storage.emplace_back(std::in_place_type<ByteStore>, c.capacity / c.banks);
            else storage.emplace_back(std::in_place_type<SparseStore>, c.capacity / c.banks);
            arbiters.emplace_back(c.ports, c.arbitration == "rr" ? Arbiter::Policy::round_robin : Arbiter::Policy::priority);
        }
        if (trace == "-") generate();
        else { std::ifstream input(trace); workload = WorkloadTrace::read(input, c.period_ps); }
        for (std::size_t i = 0; i < workload.size(); ++i) {
            const auto& t = workload[i].transaction;
            require(t.metadata.source < c.ports && t.data.size() == c.bytes && t.address % c.bytes == 0 &&
                    t.address < c.capacity && t.data.size() <= c.capacity - t.address, "workload interface/range mismatch");
            const auto first = mapper.map(t.address), last = mapper.map(t.address + t.data.size() - 1);
            require(first.bank == last.bank && first.local + t.data.size() - 1 == last.local, "profile rejects cross-bank requests");
            ports[t.metadata.source].push_back(i);
        }
        SC_THREAD(run);
    }
    sc_time duration(std::uint64_t cycles) const {
        require(cycles <= UINT64_MAX / config.period_ps, "time overflow");
        return sc_time::from_value(cycles * config.period_ps); // sc_main fixes 1 ps resolution
    }
    void generate() {
        auto pattern = config.pattern == "random" ? TrafficSource::Pattern::random : config.pattern == "hotspot" ? TrafficSource::Pattern::hotspot :
                       config.pattern == "sequential" ? TrafficSource::Pattern::sequential : TrafficSource::Pattern::stride;
        std::vector<TrafficSource> sources;
        std::vector<BurstSchedule> schedules;
        for (unsigned p = 0; p < config.ports; ++p) {
            sources.emplace_back(p * (config.capacity / config.ports), config.capacity / config.ports, config.bytes,
                                 pattern, config.stride, config.write_percent, config.seed, "port" + std::to_string(p));
            schedules.emplace_back(config.requests, config.burst_requests, config.burst_period_cycles,
                                   std::uint64_t(p) * config.phase_cycles);
            (void)schedules.back().total_bytes(config.bytes);
        }
        for (unsigned i = 0; i < config.requests; ++i) for (unsigned p = 0; p < config.ports; ++p) {
            auto transaction = sources[p].next();
            transaction.metadata.id = std::uint64_t(i) * config.ports + p;
            transaction.metadata.source = p;
            workload.push_back({std::move(transaction), schedules[p].earliest(i), {}});
        }
    }
    void event(const Transaction& transaction, const std::string& phase, const std::string& resource, std::uint64_t value = 0) {
        events.emit(transaction.metadata.id, 0, "port" + std::to_string(transaction.metadata.source), phase, resource, value);
    }
    void transfer(Flight& flight) {
        auto& t = flight.transaction;
        auto location = mapper.map(t.address);
        auto enables = t.mask.empty() ? nullptr : t.mask.data();
        const bool okay = std::visit([&](auto& memory) {
            return t.write ? memory.write(location.local, t.data.data(), t.data.size(), enables, t.mask.size()) :
                             memory.read(location.local, t.data.data(), t.data.size(), enables, t.mask.size());
        }, storage[flight.bank]);
        require(okay, "storage transfer failed");
        if (t.write) scoreboard.write_committed(t); else scoreboard.check_read(t);
        event(t, t.write ? "write" : "read", "bank" + std::to_string(flight.bank), t.data.size());
        event(t, "complete", "bank" + std::to_string(flight.bank), t.data.size());
        flight.visible = true;
    }
    void verify_memory() {
        Transaction actual;
        actual.data.resize(config.capacity);
        for (std::uint64_t address = 0; address < config.capacity; ++address) {
            const auto mapped = mapper.map(address);
            require(mapper.inverse(mapped.bank, mapped.local) == address, "mapping inverse mismatch");
            auto& byte = actual.data[address];
            require(std::visit([&](const auto& memory) { return memory.read(mapped.local, &byte, 1); }, storage[mapped.bank]), "final read");
            data_hash ^= byte; data_hash *= 1099511628211ULL;
        }
        scoreboard.check_read(actual);
        conservation.finish();
    }
    void run() {
        lifecycle.warmup();
        for (cycle = 0; cycle < config.max_cycles; ++cycle) {
            if (cycle == config.warmup_cycles) lifecycle.measure();
            for (auto& flight : flights) {
                if (!flight.visible && sc_time_stamp() >= flight.ticket.ready) transfer(flight);
                if (flight.visible && !flight.returned && sc_time_stamp() >= flight.ticket.ready + duration(config.response_cycles)) {
                    completions.complete(flight.transaction.metadata.id, true);
                    event(flight.transaction, "response_ready", "return");
                    flight.returned = true;
                }
            }
            // Finite response throughput: one result per port per cycle, in stream order.
            for (unsigned p = 0; p < config.ports; ++p) if (auto result = completions.retire(p)) {
                auto it = std::find_if(flights.begin(), flights.end(), [&](const Flight& flight) { return flight.transaction.metadata.id == result->tag; });
                require(it != flights.end(), "retirement without owned payload");
                resources[it->bank]->retire(it->ticket.id);
                conservation.complete(result->tag, it->transaction.data.size());
                if (it->accepted_cycle >= config.warmup_cycles) { ++measured; measured_latency += cycle - it->accepted_cycle; }
                event(it->transaction, "retire", "return", it->transaction.data.size());
                retired.insert(result->tag); flights.erase(it);
            }
            if (retired.size() == workload.size() && cycle >= config.warmup_cycles) {
                lifecycle.stop_injection();
                verify_memory();
                lifecycle.finish(completions.barrier_ready() && flights.empty());
                passed = true; sc_stop(); return;
            }
            std::vector<bool> eligible(config.ports, false);
            for (unsigned p = 0; p < config.ports; ++p) if (next[p] < ports[p].size()) {
                const auto& request = workload[ports[p][next[p]]];
                bool ready = request.earliest_cycle <= cycle;
                for (auto dependency : request.dependencies) ready = ready && retired.count(dependency);
                if (!ready) continue;
                eligible[p] = true;
                if (!queued[p]) { event(request.transaction, "enqueue", "ingress", request.transaction.address); queued[p] = true; }
            }
            for (unsigned bank = 0; bank < config.banks; ++bank) {
                std::vector<bool> ready(config.ports, false);
                for (unsigned p = 0; p < config.ports; ++p)
                    ready[p] = eligible[p] && mapper.map(workload[ports[p][next[p]]].transaction.address).bank == bank;
                if (std::none_of(ready.begin(), ready.end(), [](bool value) { return value; })) continue;
                const bool full = completions.outstanding() == config.outstanding;
                auto ticket = full ? std::optional<ResourceTiming::Ticket>{} : resources[bank]->reserve();
                if (!ticket) {
                    const std::string reason = full ? "wait_response_credit" :
                        resources[bank]->outstanding() == config.bank_capacity ? "wait_bank_capacity" : "wait_bank_interval";
                    for (unsigned p = 0; p < config.ports; ++p) if (ready[p])
                        event(workload[ports[p][next[p]]].transaction, reason, "bank" + std::to_string(bank));
                    continue;
                }
                const auto winner = *arbiters[bank].grant(ready);
                for (unsigned p = 0; p < config.ports; ++p) if (ready[p] && p != winner)
                    event(workload[ports[p][next[p]]].transaction, "wait_arbitration", "bank" + std::to_string(bank));
                auto transaction = workload[ports[winner][next[winner]++]].transaction;
                require(completions.submit(transaction.metadata.id, winner), "ROB admission lost credit");
                conservation.accept(transaction.metadata.id, transaction.data.size());
                event(transaction, "accept", "bank" + std::to_string(bank), transaction.address);
                event(transaction, "service", "bank" + std::to_string(bank), transaction.data.size());
                flights.push_back({std::move(transaction), bank, *ticket, false, false, cycle});
                ++accepted; eligible[winner] = false; queued[winner] = false;
            }
            wait(duration(1));
        }
        throw std::runtime_error("watchdog: workload not drained; inspect pending.json and event trace");
    }
    void save(const std::string& prefix) {
        std::ofstream trace(prefix + ".events.csv"); events.write(trace);
        std::ofstream plan(prefix + ".workload.trace"); WorkloadTrace::write(plan, config.period_ps, workload);
        std::ofstream summary(prefix + ".summary.json");
        summary << "{\"status\":\"" << (passed ? "PASS" : "FAIL") << "\",\"backend\":\"SystemC\",\"systemc_version\":\"" << sc_version()
                << "\",\"accepted\":" << accepted << ",\"completed\":" << retired.size() << ",\"finish_cycle\":" << cycle
                << ",\"period_ps\":" << config.period_ps << ",\"measure_begin_cycle\":" << config.warmup_cycles
                << ",\"measured_transactions\":" << measured << ",\"measured_latency_cycles\":" << measured_latency
                << ",\"memory_hash\":\"" << data_hash << "\",\"trace_dropped\":" << events.dropped() << ",\"event_counts\":{";
        bool first = true;
        for (const auto& item : events.counts()) { if (!first) summary << ','; first = false; summary << '"' << item.first << "\":" << item.second; }
        summary << "}}\n";
        std::ofstream pending(prefix + ".pending.json");
        pending << "{\"schema_version\":1,\"cycle\":" << cycle
                << ",\"period_ps\":" << config.period_ps
                << ",\"outstanding\":" << completions.outstanding()
                << ",\"capacity\":" << config.outstanding << ",\"ports\":[";
        for (unsigned p = 0; p < config.ports; ++p) {
            if (p) pending << ',';
            pending << "{\"port\":" << p << ",\"remaining\":" << ports[p].size() - next[p]
                    << ",\"queued\":" << (queued[p] ? "true" : "false");
            if (next[p] < ports[p].size()) {
                const auto& request = workload[ports[p][next[p]]];
                pending << ",\"next_id\":" << request.transaction.metadata.id << ",\"earliest_cycle\":" << request.earliest_cycle << ",\"waiting_on\":[";
                bool separator = false;
                for (auto dependency : request.dependencies) if (!retired.count(dependency)) { if (separator) pending << ','; separator = true; pending << dependency; }
                pending << ']';
            }
            pending << '}';
        }
        pending << "],\"banks\":[";
        for (unsigned bank = 0; bank < config.banks; ++bank) {
            if (bank) pending << ',';
            pending << "{\"bank\":" << bank << ",\"outstanding\":" << resources[bank]->outstanding()
                    << ",\"capacity\":" << config.bank_capacity << '}';
        }
        pending << "],\"flights\":[";
        first = true;
        for (const auto& flight : flights) {
            if (!first) pending << ',';
            first = false;
            const auto& transaction = flight.transaction;
            // Report processed state, not a prediction based on the snapshot time.
            // At max_cycles, events due at that boundary have not been processed.
            pending << "{\"id\":" << transaction.metadata.id << ",\"source\":" << transaction.metadata.source
                    << ",\"bank\":" << flight.bank << ",\"accepted_cycle\":" << flight.accepted_cycle
                    << ",\"service_ready_cycle\":" << flight.ticket.ready.value() / config.period_ps
                    << ",\"response_ready_cycle\":" << (flight.ticket.ready + duration(config.response_cycles)).value() / config.period_ps
                    << ",\"stage\":\"" << (flight.returned ? "retirement" : flight.visible ? "response" : "service") << "\"}";
        }
        pending << "]}\n";
        require(summary.good() && pending.good(), "result write failed");
    }
};
int sc_main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::invalid_argument("usage: multibank_run CONFIG.cfg INPUT.trace|- OUTPUT_PREFIX");
        sc_set_time_resolution(1, SC_PS);
        MultiBank system("multibank", Config::read(argv[1]), argv[2]);
        try { sc_start(); require(system.passed, "simulation ended before drain"); }
        catch (const std::exception& error) { system.save(argv[3]); std::cerr << error.what() << '\n'; return 1; }
        system.save(argv[3]); return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
