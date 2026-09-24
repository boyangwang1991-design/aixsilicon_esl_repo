#pragma once
#include <npu_mesh/config.hpp>
#include <systemc>
#include <memory>
#include <functional>
namespace aix::esl::npu_mesh {
// One clocked subsystem, containing independently accounted router/link/NIU/target resources.
class Model final : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(Model);
    explicit Model(sc_core::sc_module_name, Config = {});
    ~Model() override;
    Submission submit(const Request&);
    Status validate_request(const Request&) const;
    bool take(std::uint64_t handle, Completion&);
    const sc_core::sc_event& completion_event() const;
    bool idle() const; // Includes completion queues and command queues.
    void drain();
    bool resume();
    void reset(); // Abort in-flight; flush local modeled network; advance epoch, preserve memory.
    bool reconfigure_map(const std::vector<Endpoint>&);
    std::uint64_t reserve(unsigned context, std::uint64_t address, unsigned length);
    bool release(std::uint64_t token);
    bool pin(std::uint64_t token);
    void unpin(std::uint64_t token);
    bool poisoned(std::uint64_t token) const;
    void stall_target(unsigned endpoint, bool);
    void corrupt_next(unsigned endpoint);
    // Initialization/debug access: idle only; never counted as real target traffic.
    void initialize(std::uint64_t address, const std::vector<unsigned char>&);
    std::vector<unsigned char> inspect(std::uint64_t address, unsigned length) const;
    bool command(unsigned tile, std::uint64_t value);
    bool take_command(unsigned tile, std::uint64_t& value);
    const Metrics& metrics() const;
    const std::vector<PortMetrics>& ports() const;
    const std::vector<Trace>& trace() const;
    const Config& config() const;
    std::uint64_t epoch() const;
    void target_report(unsigned endpoint, std::ostream&) const;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
    void run();
};
// Bounded command shell, source-replication cache, no scheduler or allocation policy.
class TensorDma final : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(TensorDma);
    TensorDma(sc_core::sc_module_name, Model&, unsigned capacity = 8);
    ~TensorDma() override;
    Submission submit(const DmaDescriptor&);
    bool take(std::uint64_t, DmaCompletion&);
    void cancel(std::uint64_t);
    bool idle() const;
    std::uint64_t source_bytes() const;
    static DmaDescriptor strided(unsigned source, unsigned context, unsigned id,
        std::uint64_t from, std::uint64_t to, unsigned rows, unsigned row_bytes,
        unsigned source_stride, unsigned destination_stride);
private:
    struct Impl;
    std::unique_ptr<Impl> p;
    void run();
};
// Epoch-scoped completion/barrier state, independent of the data network.
class SyncEvents {
public:
    bool arrive(unsigned context, std::uint64_t epoch, std::uint64_t event,
                unsigned participant, Status status);
    Status query(unsigned context, std::uint64_t epoch, std::uint64_t event,
                 std::uint64_t expected) const;
    void reset(std::uint64_t epoch);
private:
    struct Entry { unsigned context; std::uint64_t epoch, event, mask; Status status; };
    std::vector<Entry> entries;
    std::uint64_t epoch_ = 0;
};
}
