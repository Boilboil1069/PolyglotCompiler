cmake_minimum_required(VERSION 3.20)
project(PolyglotCompiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
include(${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake)

add_library(polyglot_common
    common/src/core/type_system.cpp
    common/src/core/symbol_table.cpp
    backends/common/src/debug_info.cpp
    backends/common/src/debug_emitter.cpp
)

target_include_directories(polyglot_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(polyglot_common PUBLIC
    fmt::fmt-header-only
    nlohmann_json::nlohmann_json
)

add_library(frontend_common
    frontends/common/src/token_pool.cpp
    frontends/common/src/preprocessor.cpp
)

target_include_directories(frontend_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_common PUBLIC polyglot_common)

add_library(frontend_python
    frontends/python/src/lexer/lexer.cpp
    frontends/python/src/parser/parser.cpp
    frontends/python/src/sema/sema.cpp
)

target_include_directories(frontend_python PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_python PUBLIC frontend_common polyglot_common)

add_library(frontend_cpp
    frontends/cpp/src/lexer/lexer.cpp
    frontends/cpp/src/parser/parser.cpp
    frontends/cpp/src/sema/sema.cpp
    frontends/cpp/src/lowering/lowering.cpp
)

target_include_directories(frontend_cpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_cpp PUBLIC frontend_common polyglot_common)

add_library(frontend_rust
    frontends/rust/src/lexer/lexer.cpp
    frontends/rust/src/parser/parser.cpp
    frontends/rust/src/sema/sema.cpp
)

target_include_directories(frontend_rust PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_rust PUBLIC frontend_common polyglot_common)

add_library(middle_ir
    middle/src/ir/builder.cpp
    middle/src/ir/ir_context.cpp
    middle/src/ir/cfg.cpp
    middle/src/ir/analysis.cpp
    middle/src/ir/data_layout.cpp
    middle/src/ir/ssa.cpp
    middle/src/ir/verifier.cpp
    middle/src/ir/printer.cpp
    middle/src/ir/parser.cpp
    middle/src/ir/passes/opt.cpp
    middle/src/passes/pass_manager.cpp
    middle/src/passes/optimizations/constant_fold.cpp
    middle/src/passes/optimizations/dead_code_elim.cpp
    middle/src/passes/optimizations/common_subexpr.cpp
    middle/src/passes/optimizations/inlining.cpp
)

target_include_directories(middle_ir PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(middle_ir PUBLIC polyglot_common)

add_library(backend_x86_64
    backends/x86_64/src/asm_printer/emit.cpp
    backends/x86_64/src/asm_printer/scheduler.cpp
    backends/x86_64/src/isel/isel.cpp
    backends/x86_64/src/regalloc/graph_coloring.cpp
    backends/x86_64/src/regalloc/linear_scan.cpp
)

target_include_directories(backend_x86_64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend_x86_64 PUBLIC polyglot_common)

add_library(backend_arm64
    backends/arm64/src/asm_printer/emit.cpp
    backends/arm64/src/asm_printer/scheduler.cpp
    backends/arm64/src/isel/isel.cpp
    backends/arm64/src/regalloc/graph_coloring.cpp
    backends/arm64/src/regalloc/linear_scan.cpp
)

target_include_directories(backend_arm64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend_arm64 PUBLIC polyglot_common)

add_library(runtime
    runtime/src/gc/mark_sweep.cpp
    runtime/src/gc/generational.cpp
    runtime/src/gc/gc_strategy.cpp
    runtime/src/gc/runtime.cpp
    runtime/src/interop/ffi.cpp
    runtime/src/interop/memory.cpp
    runtime/src/interop/marshalling.cpp
    runtime/src/interop/type_mapping.cpp
    runtime/src/interop/calling_convention.cpp
    runtime/src/libs/base.c
    runtime/src/libs/base_gc_bridge.cpp
    runtime/src/libs/python_rt.c
    runtime/src/libs/cpp_rt.c
    runtime/src/libs/rust_rt.c
    runtime/src/services/exception.cpp
    runtime/src/services/threading.cpp
)

target_include_directories(runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(polyc
    tools/polyc/src/driver.cpp
)

target_link_libraries(polyc
    frontend_python
    frontend_cpp
    frontend_rust
    backend_x86_64
    backend_arm64
    middle_ir
    runtime
)

add_executable(polyasm
    tools/polyasm/src/assembler.cpp
)

target_link_libraries(polyasm
    backend_x86_64
    backend_arm64
    middle_ir
    polyglot_common
)

add_executable(polyld
    tools/polyld/src/linker.cpp
)

target_link_libraries(polyld
    polyglot_common
)

add_executable(polyopt
    tools/polyopt/src/optimizer.cpp
)

target_link_libraries(polyopt
    middle_ir
    backend_x86_64
    backend_arm64
    polyglot_common
)

add_executable(polyrt
    tools/polyrt/src/runtime_tool.cpp
)

target_link_libraries(polyrt
    runtime
    polyglot_common
)

file(GLOB_RECURSE UNIT_TEST_SOURCES tests/unit/*.cpp)
add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE
    polyglot_common
    frontend_python
    frontend_cpp
    frontend_rust
    middle_ir
    backend_x86_64
    backend_arm64
    runtime
    Catch2::Catch2
    Catch2::Catch2WithMain
)
enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
