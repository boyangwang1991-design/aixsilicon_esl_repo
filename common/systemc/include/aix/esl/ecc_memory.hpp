#pragma once
#include <aix/esl/secded.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace aix::esl {
// Functional SECDED service over any ByteStore-compatible backend. No wait(),
// hidden timing or automatic scrub. The composing SystemC owner serializes access.
template<class Storage> class EccMemory {
public:
    using Status = Secded64::Status;
    struct Cost {
        std::uint64_t reads = 0, writes = 0, decodes = 0, encodes = 0;
    };
    struct Result {
        Status status = Status::clean;
        std::uint64_t data = 0;
        bool committed = false;
        Cost cost;
    };
    struct Statistics {
        Cost cost;
        std::uint64_t corrected = 0, uncorrectable = 0, rmw = 0;
        std::uint64_t scrubbed = 0, repairs = 0, injected_bits = 0;
    };
    explicit EccMemory(std::size_t bytes) : storage_(checked_size(bytes)) {}
    std::uint64_t words() const noexcept { return storage_.size() / 8; }
    const Statistics& statistics() const noexcept { return statistics_; }

    // Corrected reads return corrected data without repairing the physical word.
    Result read_word(std::uint64_t word) {
        bounds(word);
        Result result = decode(word);
        account(result.cost);
        return result;
    }
    // Byte mask bit i corresponds to little-endian data byte i. A partial write
    // decodes the old word before merging; an uncorrectable old word aborts it.
    Result write_word(std::uint64_t word, std::uint64_t data, std::uint8_t mask = 0xff) {
        bounds(word);
        Result result;
        if (!mask) return result;
        if (mask != 0xff) {
            ++statistics_.rmw;
            result = decode(word);
            if (result.status == Status::uncorrectable) {
                account(result.cost);
                return result;
            }
            for (unsigned byte = 0; byte < 8; ++byte) {
                const auto lane = std::uint64_t(0xff) << (8 * byte);
                if (mask & (1u << byte)) result.data = (result.data & ~lane) | (data & lane);
            }
        } else result.data = data;
        commit(word, result.data);
        result.committed = true;
        ++result.cost.writes;
        ++result.cost.encodes;
        account(result.cost);
        return result;
    }
    // One codeword per call: the owner determines scrub period, scheduling and
    // fairness with demand traffic. An uncorrectable word is never rewritten.
    Result scrub_word(std::uint64_t word) {
        bounds(word);
        ++statistics_.scrubbed;
        Result result = decode(word);
        if (result.status == Status::corrected) {
            commit(word, result.data);
            result.committed = true;
            ++result.cost.writes;
            ++result.cost.encodes;
            ++statistics_.repairs;
        }
        account(result.cost);
        return result;
    }
    void inject_bit(std::uint64_t word, unsigned bit) {
        bounds(word);
        if (bit >= 72) throw std::out_of_range("ECC bit index");
        if (bit < 64) {
            auto data = raw_data(word) ^ (std::uint64_t(1) << bit);
            write_raw(word, data); // parity deliberately unchanged
        } else {
            checks_[word] ^= static_cast<std::uint8_t>(1u << (bit - 64));
        }
        ++statistics_.injected_bits;
    }
    void clear() {
        storage_.clear();
        checks_.clear();
        statistics_ = {};
    }
private:
    Storage storage_;
    std::map<std::uint64_t, std::uint8_t> checks_;
    Statistics statistics_;
    static std::size_t checked_size(std::size_t bytes) {
        if (!bytes || bytes % 8) throw std::invalid_argument("ECC storage must contain whole 8-byte words");
        return bytes;
    }
    void bounds(std::uint64_t word) const {
        if (word >= words()) throw std::out_of_range("ECC word index");
    }
    std::uint64_t raw_data(std::uint64_t word) const {
        std::array<unsigned char, 8> bytes{};
        if (!storage_.read(word * 8, bytes.data(), bytes.size()))
            throw std::logic_error("ECC backing read violated storage contract");
        std::uint64_t data = 0;
        for (unsigned i = 0; i < 8; ++i) data |= std::uint64_t(bytes[i]) << (8 * i);
        return data;
    }
    void write_raw(std::uint64_t word, std::uint64_t data) {
        std::array<unsigned char, 8> bytes{};
        for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<unsigned char>(data >> (8 * i));
        if (!storage_.write(word * 8, bytes.data(), bytes.size()))
            throw std::logic_error("ECC backing write violated storage contract");
    }
    void commit(std::uint64_t word, std::uint64_t data) {
        const auto encoded = Secded64::encode(data);
        auto entry = checks_.try_emplace(word, 0).first;
        write_raw(word, data);
        entry->second = encoded.check;
    }
    Result decode(std::uint64_t word) {
        auto it = checks_.find(word);
        const auto decoded = Secded64::decode({raw_data(word), it == checks_.end() ? std::uint8_t(0) : it->second});
        if (decoded.status == Status::corrected) ++statistics_.corrected;
        if (decoded.status == Status::uncorrectable) ++statistics_.uncorrectable;
        return {decoded.status, decoded.status == Status::uncorrectable ? 0 : decoded.data,
                false, {1, 0, 1, 0}};
    }
    void account(const Cost& cost) {
        statistics_.cost.reads += cost.reads;
        statistics_.cost.writes += cost.writes;
        statistics_.cost.decodes += cost.decodes;
        statistics_.cost.encodes += cost.encodes;
    }
};
}
