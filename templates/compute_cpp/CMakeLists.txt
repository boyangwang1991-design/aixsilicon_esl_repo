# Existing executable template; reusable model-library packaging is tracked in SC01/SC03.
cmake_minimum_required(VERSION 3.19)
project({{name}} LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ESLSystemC.cmake")
add_executable({{name}} model.cpp)
target_compile_features({{name}} PRIVATE cxx_std_${ESL_CXX_STANDARD})
target_link_libraries({{name}} PRIVATE SystemC::systemc)
