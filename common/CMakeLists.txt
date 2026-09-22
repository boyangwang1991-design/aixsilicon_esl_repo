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
# Source implementations have one owner; public include names remain flat.
foreach(layer primitives infrastructure adapters services workloads verification)
    target_include_directories(aix_esl_common INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../${layer}/systemc/include>)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../${layer}/systemc/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.hpp"
        PATTERN "mmio32.hpp" EXCLUDE)
endforeach()
install(FILES README.md common.yaml DESTINATION "${CMAKE_INSTALL_DATADIR}/aix-esl/common")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DATADIR}/aix-esl/common/docs")
install(FILES ../contracts/common_primitives.md ../contracts/multibank.md ../contracts/multibank.schema.json
    DESTINATION "${CMAKE_INSTALL_DATADIR}/aix-esl/contracts")
