cmake_minimum_required(VERSION 3.19)
project(AixEslTlmBus VERSION 0.1.0 LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ESLModel.cmake")
esl_model(tlm_bus AixEslTlmBus)
