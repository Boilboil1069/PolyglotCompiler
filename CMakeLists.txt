cmake_minimum_required(VERSION 3.20)
project(PolyglotCompiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(polyglot_common
    common/src/core/type_system.cpp
    common/src/core/symbol_table.cpp
)

target_include_directories(polyglot_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(frontend_common
    frontends/common/src/token_pool.cpp
)

target_include_directories(frontend_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_common PUBLIC polyglot_common)

add_library(frontend_python
    frontends/python/src/lexer/lexer.cpp
    frontends/python/src/parser/parser.cpp
)

target_include_directories(frontend_python PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_python PUBLIC frontend_common polyglot_common)

add_library(middle_ir
    middle/src/ir/builder.cpp
    middle/src/ir/verifier.cpp
    middle/src/passes/pass_manager.cpp
)

target_include_directories(middle_ir PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(middle_ir PUBLIC polyglot_common)

add_library(backend_x86_64
    backends/x86_64/src/asm_printer/emit.cpp
)

target_include_directories(backend_x86_64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend_x86_64 PUBLIC polyglot_common)

add_library(runtime
    runtime/src/gc/mark_sweep.cpp
    runtime/src/gc/generational.cpp
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
    backend_x86_64
    middle_ir
    runtime
)
