#pragma once
#include <aix/esl/blocking_gate.hpp>
#include <systemc>
#include <tlm>
#include <cstdint>
#include <limits>

namespace aix::esl {
// Private implementation helper: one in-flight word access, completion-time
// effects, little endian, full-word byte enables only. Never retains a payload.
class Mmio32 {
public:
    explicit Mmio32(sc_core::sc_time latency) : latency_(latency), gate_(1) {}
    bool idle() const { return gate_.idle(); }
    const sc_core::sc_event& idle_event() const { return gate_.idle_event(); }
    void drain() { open_ = false; gate_.drain(); }
    bool accepting() const { return open_ && !held_; }
    bool resume() {
        if (held_ || !gate_.resume()) return false;
        open_ = true;
        return true;
    }
    void reset(bool held) {
        held_ = held;
        if (held) { ++epoch_; drain(); reset_event_.notify(sc_core::SC_ZERO_TIME); }
    }
    template<class Apply>
    void access(tlm::tlm_generic_payload& tx, sc_core::sc_time& delay, Apply apply) {
        tx.set_dmi_allowed(false);
        auto reject = [&](tlm::tlm_response_status response) { tx.set_response_status(response); };
        if (!tx.is_read() && !tx.is_write()) { reject(tlm::TLM_COMMAND_ERROR_RESPONSE); return; }
        if (!tx.get_data_ptr() || tx.get_data_length() != 4 || tx.get_streaming_width() < 4) {
            reject(tlm::TLM_BURST_ERROR_RESPONSE); return;
        }
        if (tx.get_address() % 4) { reject(tlm::TLM_ADDRESS_ERROR_RESPONSE); return; }
        if (auto* be = tx.get_byte_enable_ptr()) {
            if (!tx.get_byte_enable_length()) { reject(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return; }
            for (unsigned i = 0; i < 4; ++i)
                if (be[i % tx.get_byte_enable_length()] != 0xff) {
                    reject(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE); return;
                }
        }
        auto process = sc_core::sc_get_current_process_handle();
        const auto remaining = std::numeric_limits<std::uint64_t>::max() - sc_core::sc_time_stamp().value();
        if (delay.value() > remaining || latency_.value() > remaining - delay.value()) {
            reject(tlm::TLM_GENERIC_ERROR_RESPONSE); return;
        }
        if (!process.valid() || process.proc_kind() == sc_core::SC_METHOD_PROC_ ||
            !accepting() || !gate_.enter()) { reject(tlm::TLM_GENERIC_ERROR_RESPONSE); return; }
        BlockingLease lease(gate_);
        const auto epoch = epoch_;
        const auto incoming = delay;
        delay = sc_core::SC_ZERO_TIME;
        auto wait_interval = [&](sc_core::sc_time duration) {
            auto end = sc_core::sc_time_stamp() + duration;
            while (sc_core::sc_time_stamp() < end) {
                sc_core::wait(end - sc_core::sc_time_stamp(), reset_event_);
                if (epoch != epoch_) return false;
            }
            return epoch == epoch_;
        };
        if (!wait_interval(incoming) || !wait_interval(latency_)) {
            reject(tlm::TLM_GENERIC_ERROR_RESPONSE); return;
        }
        auto* data = tx.get_data_ptr();
        std::uint32_t value = 0;
        if (tx.is_write())
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(data[i]) << (8 * i);
        auto status = apply(tx.get_address(), tx.is_write(), value);
        if (status == tlm::TLM_OK_RESPONSE && tx.is_read())
            for (unsigned i = 0; i < 4; ++i) data[i] = (value >> (8 * i)) & 0xff;
        reject(status);
    }
private:
    sc_core::sc_time latency_;
    BlockingGate gate_;
    sc_core::sc_event reset_event_;
    std::uint64_t epoch_ = 0;
    bool held_ = false, open_ = true;
};
}
