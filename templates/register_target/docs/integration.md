# Integration

Required target socket registers, TLM-2 LT/32-bit. Public headers `<{{name}}/model.hpp>` and `<{{name}}/config.hpp>`. Library target `aix::esl::{{name}}`; installed package `{{package}}`. No sc_main in the library.

Source consumer: `cmake -S examples/integration -B build/consumer -DESL_MODEL_SOURCE_DIR="$PWD"`, then build and ctest.
Install with root CMake and a temporary prefix. Configure the same consumer without ESL_MODEL_SOURCE_DIR and with CMAKE_PREFIX_PATH pointing at that prefix; it uses find_package. Repeat after moving the prefix. SystemC 3.0.2/C++17 must be supplied by the environment.
