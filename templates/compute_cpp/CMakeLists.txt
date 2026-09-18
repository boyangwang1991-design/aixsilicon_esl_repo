# {{name}}：SystemC 薄骨架构建（compute_cpp 模板）
# 用法：cmake -DSYSTEMC_HOME=$SYSTEMC_HOME -S . -B build && cmake --build build
cmake_minimum_required(VERSION 3.16)
project({{name}} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED SYSTEMC_HOME AND DEFINED ENV{SYSTEMC_HOME})
    set(SYSTEMC_HOME "$ENV{SYSTEMC_HOME}")
endif()
if(SYSTEMC_HOME)
    set(CMAKE_PREFIX_PATH "${SYSTEMC_HOME};${CMAKE_PREFIX_PATH}")
endif()
find_library(SYSTEMC_LIB NAMES systemc HINTS ${SYSTEMC_HOME}/lib-linux64 REQUIRED)
find_path(SYSTEMC_INCLUDE systemc.h HINTS ${SYSTEMC_HOME}/include REQUIRED)

add_executable({{name}} model.cpp)
target_include_directories({{name}} PRIVATE ${SYSTEMC_INCLUDE})
target_link_libraries({{name}} PRIVATE ${SYSTEMC_LIB})
