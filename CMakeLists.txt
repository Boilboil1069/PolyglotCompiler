cmake_minimum_required(VERSION 3.20)
project(PolyglotCompiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
include(${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake)

add_library(polyglot_common
    common/src/core/type_system.cpp
    common/src/core/symbol_table.cpp
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
    middle/src/ir/ssa.cpp
    middle/src/ir/verifier.cpp
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
    backends/x86_64/src/regalloc/linear_scan.cpp
)

target_include_directories(backend_x86_64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend_x86_64 PUBLIC polyglot_common)

add_library(runtime
    runtime/src/gc/mark_sweep.cpp
    runtime/src/gc/generational.cpp
    runtime/src/interop/ffi.cpp
    runtime/src/libs/base.c
    runtime/src/libs/python_rt.c
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
    middle_ir
    runtime
)
