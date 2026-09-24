#pragma once
#include <npu_mesh/config.hpp>
#include <deque>
#include <map>
#include <memory>
#include <functional>
namespace aix::esl::npu_mesh {
struct Packet {
    std::uint64_t packet = 0, handle = 0, epoch = 0;
    unsigned source = 0, destination = 0, endpoint = 0, vn = 0, bytes = 0, offset = 0;
    unsigned context = 0;
    Status status = Status::ok;
    mutable bool endpoint_reserved = false;
    std::uint64_t address = 0;
    std::vector<unsigned char> data, enables;
};
class Network {
    struct Flit { std::shared_ptr<Packet> packet; unsigned index, count; std::uint64_t ready; };
    struct Buffer { std::deque<Flit> fifo; unsigned credit; std::uint64_t owner = 0; };
    struct Transit { std::uint64_t due; unsigned buffer; Flit flit; };
    struct Credit { std::uint64_t due; unsigned buffer; };
    struct Injection { std::shared_ptr<Packet> packet; unsigned sent = 0, count = 0, buffer = 0; };
    Config cfg;
    Metrics& metrics;
    unsigned routers, lanes, channels;
    std::vector<Buffer> buffers;
    std::deque<Transit> transit;
    std::deque<Credit> credits;
    std::vector<std::deque<Injection>> injection;
    std::vector<unsigned> rr;
    std::uint64_t next_packet = 1;
    unsigned index(unsigned router, unsigned port, unsigned lane) const;
    unsigned route(unsigned router, unsigned destination) const;
    unsigned next_buffer(unsigned router, unsigned out, unsigned lane) const;
    unsigned width(unsigned vn) const;
public:
    std::vector<PortMetrics> ports;
    std::function<bool(const Packet&)> admit;
    std::function<void(std::shared_ptr<Packet>)> receive;
    Network(Config, Metrics&);
    bool inject(std::shared_ptr<Packet>);
    bool injection_idle(unsigned source) const;
    void tick(std::uint64_t cycle);
    bool idle() const;
    void reset();
    void check() const;
};
}
