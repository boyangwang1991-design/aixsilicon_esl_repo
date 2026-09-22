#pragma once
#include <aix/esl/transaction.hpp>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace aix::esl {
struct WorkloadRequest {
    Transaction transaction;
    std::uint64_t earliest_cycle = 0;
    std::vector<std::uint64_t> dependencies;
};
// Portable integer-cycle workload, distinct from observation events. Dependencies
// name earlier records and release only after response retirement in the consumer.
class WorkloadTrace {
public:
    static std::uint64_t integer(const std::string& token) {
        if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("expected unsigned decimal token");
        std::size_t used = 0;
        auto value = std::stoull(token, &used);
        if (used != token.size()) throw std::invalid_argument("integer trailing data");
        return value;
    }
    static std::vector<WorkloadRequest> read(std::istream& input, std::uint64_t period_ps) {
        if (!period_ps) throw std::invalid_argument("workload period must be positive");
        std::string line;
        if (!std::getline(input, line) || line != "# aix-esl-workload-v1 period_ps=" + std::to_string(period_ps))
            throw std::invalid_argument("workload version/clock period mismatch");
        std::vector<WorkloadRequest> requests;
        std::set<std::uint64_t> seen;
        while (std::getline(input, line)) {
            if (line.empty() || requests.size() == 100000 || line.size() > 20000)
                throw std::invalid_argument("empty/oversized workload record");
            std::istringstream row(line);
            std::string tag, source, earliest, op, address, data, mask, dependencies, extra;
            if (!(row >> tag >> source >> earliest >> op >> address >> data >> mask >> dependencies) || row >> extra)
                throw std::invalid_argument("workload record columns");
            WorkloadRequest request;
            auto& transaction = request.transaction;
            transaction.metadata.id = integer(tag);
            auto port = integer(source);
            if (port > std::numeric_limits<unsigned>::max()) throw std::invalid_argument("source overflow");
            transaction.metadata.source = static_cast<unsigned>(port);
            request.earliest_cycle = integer(earliest);
            if (op != "R" && op != "W") throw std::invalid_argument("workload operation");
            transaction.write = op == "W";
            transaction.address = integer(address);
            transaction.data = decode(data);
            if (mask != "-") transaction.mask = decode(mask);
            transaction.validate();
            if (dependencies != "-") {
                std::istringstream list(dependencies);
                std::string item;
                std::set<std::uint64_t> unique;
                if (dependencies.back() == ',') throw std::invalid_argument("empty dependency");
                while (std::getline(list, item, ',')) {
                    auto dependency = integer(item);
                    if (!seen.count(dependency) || !unique.insert(dependency).second)
                        throw std::invalid_argument("unknown/forward/duplicate dependency");
                    request.dependencies.push_back(dependency);
                }
            }
            if (!seen.insert(transaction.metadata.id).second) throw std::invalid_argument("duplicate workload ID");
            requests.push_back(std::move(request));
        }
        if (!input.eof() || requests.empty()) throw std::invalid_argument("empty/unreadable workload");
        return requests;
    }
    static void write(std::ostream& output, std::uint64_t period_ps,
                      const std::vector<WorkloadRequest>& requests) {
        if (!period_ps || requests.empty() || requests.size() > 100000) throw std::invalid_argument("workload period/records");
        std::set<std::uint64_t> seen;
        output << "# aix-esl-workload-v1 period_ps=" << period_ps << '\n';
        for (const auto& request : requests) {
            const auto& transaction = request.transaction;
            transaction.validate();
            std::set<std::uint64_t> unique;
            for (auto dependency : request.dependencies)
                if (!seen.count(dependency) || !unique.insert(dependency).second)
                    throw std::invalid_argument("workload dependencies must precede consumers");
            if (!seen.insert(transaction.metadata.id).second) throw std::invalid_argument("duplicate workload ID");
            output << transaction.metadata.id << ' ' << transaction.metadata.source << ' '
                   << request.earliest_cycle << ' ' << (transaction.write ? 'W' : 'R') << ' '
                   << transaction.address << ' ' << encode(transaction.data) << ' '
                   << (transaction.mask.empty() ? "-" : encode(transaction.mask)) << ' ';
            if (request.dependencies.empty()) output << '-';
            else for (std::size_t i = 0; i < request.dependencies.size(); ++i)
                output << (i ? "," : "") << request.dependencies[i];
            output << '\n';
        }
        if (!output) throw std::runtime_error("workload write failed");
    }
private:
    static std::vector<unsigned char> decode(const std::string& text) {
        if (text.empty() || text.size() % 2 || text.size() > 8192)
            throw std::invalid_argument("workload byte vector length");
        std::vector<unsigned char> bytes;
        auto nibble = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            throw std::invalid_argument("workload requires lower-case hexadecimal");
        };
        for (std::size_t i = 0; i < text.size(); i += 2)
            bytes.push_back(static_cast<unsigned char>((nibble(text[i]) << 4) | nibble(text[i + 1])));
        return bytes;
    }
    static std::string encode(const std::vector<unsigned char>& bytes) {
        if (bytes.empty() || bytes.size() > 4096) throw std::invalid_argument("workload byte vector length");
        static const char digits[] = "0123456789abcdef";
        std::string output;
        for (auto byte : bytes) { output += digits[byte >> 4]; output += digits[byte & 15]; }
        return output;
    }
};
}
