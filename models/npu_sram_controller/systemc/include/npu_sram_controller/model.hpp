#pragma once
#include "config.hpp"
#include "mapping.hpp"
#include <systemc>
#include <functional>
#include <memory>
#include <ostream>
#include <vector>
namespace aix::esl::npu_sram_controller {
struct Request {
    unsigned port=0, id=0, beat_bytes=128, beats=1;
    uint64_t address=0, release=0;
    bool write=false;
};
struct Response {
    uint64_t token=0, accepted=0, cycle=0;
    unsigned port=0,id=0,beat=0;
    bool write=false,last=false,error=false;
    std::vector<uint8_t> data;
};
struct Metrics {
    uint64_t accepted=0, completed=0, fragments=0, fragments_done=0;
    uint64_t read_bytes=0, write_bytes=0, external_write_bytes=0;
    uint64_t bank_services=0, rmw=0, scrub=0, corrected=0, uncorrectable=0;
    uint64_t bank_conflicts=0, conflict_wait=0, lock_wait=0, hol=0;
    uint64_t frontend_stall=0, rob_stall=0, link_stall=0, remote_bytes=0;
    uint64_t queue_peak=0, bank_peak=0, rob_peak=0, network_bytes=0;
    std::vector<uint64_t> bank_grants, port_bytes, latencies, admission_latencies;
};
class Model: public sc_core::sc_module {
public:
    SC_HAS_PROCESS(Model);
    Model(sc_core::sc_module_name name,const Config& config);
    ~Model();
    // Copy-in APIs. Zero token / false means retry without losing ownership.
    uint64_t submit(const Request& request);
    bool push_w(unsigned port,const std::vector<uint8_t>& data,
                const std::vector<uint8_t>& mask,bool last);
    bool pop(unsigned port,bool write,Response& response);
    uint64_t cycle() const;
    bool idle() const;
    void stop_scrub();
    void reset(); // quiescent only; clears memory and statistics
    void inject(uint64_t address,unsigned bit_errors);
    void set_observer(std::function<void(const std::string&)> observer);
    const Metrics& metrics() const;
    void report(std::ostream&) const;
    std::string snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    void run();
};
}
