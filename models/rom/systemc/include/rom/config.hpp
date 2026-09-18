#pragma once
#include <ram/config.hpp>
namespace aix::esl::rom {
struct Config : ram::Config { Config() { read_only = true; } };
}
