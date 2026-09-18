#pragma once
#include <ram/model.hpp>
#include <rom/config.hpp>
namespace aix::esl::rom {
// Thin read-only policy over the shared memory implementation, not a second store.
class Model final : public ram::Model {
public:
    explicit Model(sc_core::sc_module_name name, Config config = {});
};
}
