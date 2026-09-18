cmake_minimum_required(VERSION 3.19)
project(aixsilicon_esl_repo LANGUAGES CXX)

# esl_repo 基础构建入口（P01）。
# - SystemC 是目标模型主体；OFF 仅供检查历史参考资产，不代表目标验证通过。
# - OFF 不查找 SystemC；ON 必须满足环境合同，否则配置失败。

set(ESL_ENABLE_SYSTEMC OFF CACHE BOOL "Enable SystemC/TLM models (requires SystemC)")
set(ESL_SYSTEMC_PREFIX "" CACHE PATH "SystemC installation prefix")

if(ESL_ENABLE_SYSTEMC)
    if(ESL_SYSTEMC_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${ESL_SYSTEMC_PREFIX}")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ESLSystemC.cmake")
endif()

# 目标模型内部共用的 header-only 组件。
add_subdirectory(common)

# 基础 Python 参考资产无需 CMake；SystemC 模型按需构建。
message(STATUS "aixsilicon_esl_repo: Python assets run via pyproject/uv; SystemC=${ESL_ENABLE_SYSTEMC}")

if(ESL_ENABLE_SYSTEMC)
    foreach(model ram rom host_master tlm_bus timer irq_controller)
        add_subdirectory(models/${model})
    endforeach()
endif()
