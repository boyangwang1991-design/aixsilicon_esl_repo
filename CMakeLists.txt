cmake_minimum_required(VERSION 3.16)
project(aixsilicon_esl_repo LANGUAGES CXX)

# esl_repo 基础构建入口（P01）。
# - 基础阶段以纯 Python 行为参考为主，SystemC 为按需扩展（P10+）。
# - 本文件保持可配置：未安装 SystemC 时 SKIP，不阻塞 Python 资产运行。

set(ESL_ENABLE_SYSTEMC OFF CACHE BOOL "Enable SystemC/TLM models (requires SystemC)")
set(ESL_SYSTEMC_PREFIX "" CACHE PATH "SystemC installation prefix")

if(ESL_ENABLE_SYSTEMC)
    if(NOT ESL_SYSTEMC_PREFIX)
        message(FATAL_ERROR "ESL_ENABLE_SYSTEMC=ON 需要 ESL_SYSTEMC_PREFIX 指向 SystemC 安装目录")
    endif()
    find_package(SystemCLanguage QUIET PATHS ${ESL_SYSTEMC_PREFIX})
    if(NOT SystemC_FOUND)
        message(FATAL_ERROR "未找到 SystemC；请先配置环境（esl doctor 可检查）")
    endif()
endif()

# 公共组件子目录（P02+ 逐个加入；目前骨架阶段为空占位）
add_subdirectory(common)

# 基础 Python 资产无需 CMake；C++ 资产在实现后于此处登记。
message(STATUS "aixsilicon_esl_repo: Python assets run via pyproject/uv; SystemC=${ESL_ENABLE_SYSTEMC}")
