#include <aix/esl/workload_trace.hpp>
#include <systemc>
#include <sstream>
#include <iostream>
using namespace aix::esl;
static void require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
static bool rejects(const std::string& text, std::uint64_t period = 1000) {
    try { std::istringstream input(text); WorkloadTrace::read(input, period); }
    catch (const std::exception&) { return true; }
    return false;
}
int sc_main(int, char**) {
    try {
        const std::string header = "# aix-esl-workload-v1 period_ps=1000\n";
        const std::string records = "7 0 3 W 16 11223344 ff00 -\n8 1 0 R 16 00000000 - 7\n";
        std::istringstream input(header + records);
        auto requests = WorkloadTrace::read(input, 1000);
        require(requests.size() == 2 && requests[0].earliest_cycle == 3 && requests[1].dependencies == std::vector<std::uint64_t>{7}, "request/dependency parsing");
        require(requests[0].transaction.mask == std::vector<unsigned char>({255,0}), "mask decoding");
        std::ostringstream output; WorkloadTrace::write(output, 1000, requests);
        require(output.str() == header + records, "canonical workload roundtrip");
        require(rejects(header + records, 2000), "clock mismatch accepted");
        require(rejects(header + "7 0 0 W 0 00 - 8\n"), "forward dependency accepted");
        require(rejects(header + records + "7 0 0 R 0 00 - -\n"), "duplicate ID accepted");
        require(rejects(header + "7 0 0 W 0 00 01 -\n"), "illegal byte mask accepted");
        require(rejects(header + "7 0 0 W 0 0 - -\n"), "truncated hex accepted");
        require(rejects(header + "7 4294967296 0 W 0 00 - -\n"), "source overflow accepted");
        require(rejects(header + "7 0 0 W 0 00 - - extra\n"), "extra column accepted");
        require(rejects(header + "7 0 0 W 18446744073709551615 0000 - -\n"), "wrapped transfer accepted");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
