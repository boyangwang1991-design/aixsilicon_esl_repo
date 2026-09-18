#include <rom/model.hpp>
namespace aix::esl::rom {
namespace { ram::Config immutable(Config c) { c.read_only = true; return c; } }
Model::Model(sc_core::sc_module_name name, Config c) : ram::Model(name, immutable(c)) {}
}
