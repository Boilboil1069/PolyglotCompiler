cmake_minimum_required(VERSION 3.20)
project(order_risk_analyzer LANGUAGES CXX)

add_library(risk_kernel SHARED cpp/risk_kernel.cpp)
target_compile_features(risk_kernel PRIVATE cxx_std_17)
target_compile_options(risk_kernel PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>)
set_target_properties(risk_kernel PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
