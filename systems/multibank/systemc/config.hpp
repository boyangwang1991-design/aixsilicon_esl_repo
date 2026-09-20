#pragma once
#include <aix/esl/workload_trace.hpp>
#include <fstream>
#include <map>
#include <string>
#include <set>
namespace aix::esl::multibank {
// Defensive runtime contract for the resolved flat configuration. YAML/schema
// resolution is performed by tools/multibank.py; no simulation happens there.
struct Config {
    unsigned ports, banks, capacity, stripe, bytes, requests, outstanding, bank_capacity;
    unsigned latency, interval, response_cycles, period_ps, phase_cycles, warmup_cycles, max_cycles;
    unsigned write_percent, trace_limit;
    std::uint64_t seed, stride;
    std::string mapping, arbitration, storage, pattern, observation;
    static Config read(const std::string& path) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("cannot open resolved config");
        std::map<std::string, std::string> fields;
        std::string line;
        while (std::getline(input, line)) {
            auto split = line.find('=');
            if (split == std::string::npos || !fields.emplace(line.substr(0, split), line.substr(split + 1)).second)
                throw std::invalid_argument("config syntax/duplicate key");
        }
        auto take = [&](const std::string& key) {
            auto it = fields.find(key);
            if (it == fields.end()) throw std::invalid_argument("missing config: " + key);
            auto value = it->second; fields.erase(it); return value;
        };
        auto number = [&](const std::string& key) -> unsigned {
            auto value = WorkloadTrace::integer(take(key));
            if (value > 100000000) throw std::invalid_argument("config integer too large: " + key);
            return static_cast<unsigned>(value);
        };
        Config c;
        if (take("schema_version") != "1") throw std::invalid_argument("config version");
#define FIELD(name) c.name = number(#name)
        FIELD(ports); FIELD(banks); FIELD(capacity); FIELD(stripe); FIELD(bytes); FIELD(requests);
        FIELD(outstanding); FIELD(bank_capacity); FIELD(latency); FIELD(interval); FIELD(response_cycles);
        FIELD(period_ps); FIELD(phase_cycles); FIELD(warmup_cycles); FIELD(max_cycles);
        FIELD(write_percent); FIELD(trace_limit);
#undef FIELD
        c.seed = WorkloadTrace::integer(take("seed")); c.stride = WorkloadTrace::integer(take("stride"));
        c.mapping = take("mapping"); c.arbitration = take("arbitration"); c.storage = take("storage");
        c.pattern = take("pattern"); c.observation = take("observation");
        if (!fields.empty()) throw std::invalid_argument("unknown config: " + fields.begin()->first);
        c.validate(); return c;
    }
    void validate() const {
        auto require = [](bool valid, const char* reason) { if (!valid) throw std::invalid_argument(reason); };
        require(ports && ports <= 64 && banks && banks <= 64, "ports/banks must be 1..64");
        require(capacity && capacity <= 16777216 && stripe && stripe <= 4096 && bytes && bytes <= stripe,
                "capacity/stripe/request width");
        require(capacity % (std::uint64_t(banks) * stripe) == 0 && capacity % ports == 0 &&
                (capacity / ports) % bytes == 0 && stripe % bytes == 0 &&
                (capacity / banks) % bytes == 0 && stride % bytes == 0, "mapping/source alignment");
        require(requests && std::uint64_t(requests) * ports <= 100000, "workload request budget");
        require(outstanding && outstanding <= 65536 && bank_capacity && bank_capacity <= 65536, "finite credits");
        require(latency && latency <= 1000000 && interval && interval <= 1000000 && response_cycles <= 1000000,
                "resource/response timing");
        require(period_ps && period_ps <= 1000000 && max_cycles && max_cycles <= 10000000 &&
                warmup_cycles < max_cycles && phase_cycles <= max_cycles, "clock/window/watchdog");
        require(write_percent <= 100 && trace_limit <= 1000000, "write percentage/trace capacity");
        require(mapping == "stripe" || mapping == "xor" || mapping == "contiguous", "unsupported mapping");
        require(mapping != "xor" || !(banks & (banks - 1)), "XOR requires power-of-two banks");
        require(arbitration == "rr" || arbitration == "priority", "unsupported arbitration");
        require(storage == "dense" || storage == "sparse", "unsupported storage");
        require(pattern == "sequential" || pattern == "stride" || pattern == "random" || pattern == "hotspot", "unsupported workload");
        require(observation == "off" || observation == "counters" || observation == "trace", "unsupported observation");
    }
};
}
