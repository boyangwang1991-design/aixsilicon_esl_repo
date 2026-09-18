#pragma once
#include <cstddef>
namespace aix::esl::host_master {
struct Config { std::size_t max_outstanding = 4; };
}
