# min_systemc：CMake + SystemC 最小验证（K13）
# 用法：cmake -DCMAKE_PREFIX_PATH=$SYSTEMC_HOME -S . -B build && cmake --build build
cmake_minimum_required(VERSION 3.16)
project(min_systemc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 通过 SYSTEMC_HOME 或 CMAKE_PREFIX_PATH 定位 SystemC
if(NOT DEFINED SYSTEMC_HOME AND DEFINED ENV{SYSTEMC_HOME})
    set(SYSTEMC_HOME "$ENV{SYSTEMC_HOME}")
endif()
if(SYSTEMC_HOME)
    set(CMAKE_PREFIX_PATH "${SYSTEMC_HOME};${CMAKE_PREFIX_PATH}")
endif()

find_library(SYSTEMC_LIB NAMES systemc HINTS ${SYSTEMC_HOME}/lib-linux64 REQUIRED)
find_path(SYSTEMC_INCLUDE systemc.h HINTS ${SYSTEMC_HOME}/include REQUIRED)

add_executable(min_sc main.cpp)
target_include_directories(min_sc PRIVATE ${SYSTEMC_INCLUDE})
target_link_libraries(min_sc PRIVATE ${SYSTEMC_LIB})
