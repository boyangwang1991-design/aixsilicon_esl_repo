cmake_minimum_required(VERSION 3.19)
if(DEFINED ESL_ENABLE_SYSTEMC AND NOT ESL_ENABLE_SYSTEMC)
    return()
endif()
project(AixEslCommon VERSION 0.1.0 LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ESLSystemC.cmake")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(aix_esl_common INTERFACE)
add_library(aix::esl::common ALIAS aix_esl_common)
set_target_properties(aix_esl_common PROPERTIES EXPORT_NAME common)
target_compile_features(aix_esl_common INTERFACE cxx_std_${ESL_CXX_STANDARD})
target_include_directories(aix_esl_common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/systemc/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(aix_esl_common INTERFACE SystemC::systemc)
set(ESL_PACKAGE AixEslCommon)
set(ESL_DEPENDENCIES "")
set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/AixEslCommon")
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/../cmake/ESLModelConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AixEslCommonConfig.cmake" INSTALL_DESTINATION "${_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/AixEslCommonConfigVersion.cmake"
    VERSION 0.1.0 COMPATIBILITY SameMinorVersion)
install(TARGETS aix_esl_common EXPORT AixEslCommonTargets)
install(EXPORT AixEslCommonTargets NAMESPACE aix::esl:: DESTINATION "${_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AixEslCommonConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/AixEslCommonConfigVersion.cmake" DESTINATION "${_package_dir}")
install(FILES ../cmake/ESLSystemC.cmake DESTINATION "${_package_dir}/cmake")
install(FILES ../contracts/environment.json DESTINATION "${_package_dir}/contracts")
# Only these versioned public headers are exported; MMIO helper remains private.
install(FILES systemc/include/aix/esl/bounded_queue.hpp systemc/include/aix/esl/blocking_gate.hpp
    systemc/include/aix/esl/byte_store.hpp systemc/include/aix/esl/activity_monitor.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
install(FILES README.md common.yaml DESTINATION "${CMAKE_INSTALL_DATADIR}/aix-esl/common")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DATADIR}/aix-esl/common/docs")
install(FILES ../contracts/common_primitives.md DESTINATION "${CMAKE_INSTALL_DATADIR}/aix-esl/contracts")
install(FILES
    systemc/include/aix/esl/resource_timing.hpp
    systemc/include/aix/esl/address_mapper.hpp
    systemc/include/aix/esl/arbiter.hpp
    systemc/include/aix/esl/transaction.hpp
    systemc/include/aix/esl/ordered_completion.hpp
    systemc/include/aix/esl/deterministic_rng.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
install(FILES systemc/include/aix/esl/register_bank.hpp systemc/include/aix/esl/secded.hpp
    systemc/include/aix/esl/task_graph.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
install(FILES systemc/include/aix/esl/event_recorder.hpp systemc/include/aix/esl/traffic_source.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
install(FILES systemc/include/aix/esl/timed_channel.hpp systemc/include/aix/esl/sparse_store.hpp
    systemc/include/aix/esl/simulation_lifecycle.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
install(FILES systemc/include/aix/esl/verification.hpp systemc/include/aix/esl/fault_schedule.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
install(FILES systemc/include/aix/esl/storage_access.hpp systemc/include/aix/esl/ecc_memory.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
install(FILES systemc/include/aix/esl/compute_timing.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aix/esl")
