cmake_minimum_required(VERSION 3.20)
project(PolyglotCompiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-portable compile options
if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Required for runtime dynamic loading (dlopen/dlsym) and threading
find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
include(${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake)

add_library(polyglot_common
    common/src/core/type_system.cpp
    common/src/core/symbol_table.cpp
    backends/common/src/debug_info.cpp
    backends/common/src/debug_emitter.cpp
    backends/common/src/dwarf_builder.cpp
    backends/common/src/object_file.cpp
    common/src/debug/dwarf5.cpp
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
    frontends/python/src/lowering/lowering.cpp
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
    frontends/cpp/src/constexpr/cpp_constexpr.cpp
)

target_include_directories(frontend_cpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_cpp PUBLIC frontend_common polyglot_common)

add_library(frontend_rust
    frontends/rust/src/lexer/lexer.cpp
    frontends/rust/src/parser/parser.cpp
    frontends/rust/src/sema/sema.cpp
    frontends/rust/src/lowering/lowering.cpp
)

target_include_directories(frontend_rust PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_rust PUBLIC frontend_common polyglot_common)

add_library(frontend_ploy
    frontends/ploy/src/lexer/lexer.cpp
    frontends/ploy/src/parser/parser.cpp
    frontends/ploy/src/sema/sema.cpp
    frontends/ploy/src/sema/package_discovery_cache.cpp
    frontends/ploy/src/sema/command_runner.cpp
    frontends/ploy/src/lowering/lowering.cpp
)

target_include_directories(frontend_ploy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_ploy PUBLIC frontend_common polyglot_common middle_ir)

add_library(frontend_java
    frontends/java/src/lexer/lexer.cpp
    frontends/java/src/parser/parser.cpp
    frontends/java/src/sema/sema.cpp
    frontends/java/src/lowering/lowering.cpp
)

target_include_directories(frontend_java PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_java PUBLIC frontend_common polyglot_common middle_ir)

add_library(frontend_dotnet
    frontends/dotnet/src/lexer/lexer.cpp
    frontends/dotnet/src/parser/parser.cpp
    frontends/dotnet/src/sema/sema.cpp
    frontends/dotnet/src/lowering/lowering.cpp
)

target_include_directories(frontend_dotnet PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(frontend_dotnet PUBLIC frontend_common polyglot_common middle_ir)

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
    middle/src/ir/template_instantiator.cpp
    middle/src/ir/passes/opt.cpp
    middle/src/passes/pass_manager.cpp
    middle/src/passes/optimizations/constant_fold.cpp
    middle/src/passes/optimizations/dead_code_elim.cpp
    middle/src/passes/optimizations/common_subexpr.cpp
    middle/src/passes/optimizations/inlining.cpp
    middle/src/passes/optimizations/devirtualization.cpp
    middle/src/passes/optimizations/loop_optimization.cpp
    middle/src/passes/optimizations/gvn.cpp
    middle/src/passes/optimizations/advanced_optimizations.cpp
    middle/src/pgo/profile_data.cpp
    middle/src/lto/link_time_optimizer.cpp
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
    backends/x86_64/src/optimizations.cpp
    backends/x86_64/src/calling_convention.cpp
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
    backends/arm64/src/calling_convention.cpp
)

target_include_directories(backend_arm64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend_arm64 PUBLIC polyglot_common)

add_library(backend_wasm
    backends/wasm/src/wasm_target.cpp
)

target_include_directories(backend_wasm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(backend_wasm PUBLIC polyglot_common middle_ir)

add_library(runtime
    runtime/src/gc/mark_sweep.cpp
    runtime/src/gc/generational.cpp
    runtime/src/gc/copying.cpp
    runtime/src/gc/incremental.cpp
    runtime/src/gc/gc_strategy.cpp
    runtime/src/gc/runtime.cpp
    runtime/src/interop/ffi.cpp
    runtime/src/interop/memory.cpp
    runtime/src/interop/marshalling.cpp
    runtime/src/interop/type_mapping.cpp
    runtime/src/interop/calling_convention.cpp
    runtime/src/interop/container_marshal.cpp
    runtime/src/interop/object_lifecycle.cpp
    runtime/src/libs/base.c
    runtime/src/libs/base_gc_bridge.cpp
    runtime/src/libs/python_rt.c
    runtime/src/libs/cpp_rt.c
    runtime/src/libs/rust_rt.c
    runtime/src/libs/java_rt.c
    runtime/src/libs/dotnet_rt.c
    runtime/src/services/exception.cpp
    runtime/src/services/reflection.cpp
    runtime/src/services/threading.cpp
)

target_include_directories(runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(runtime PUBLIC
    ${CMAKE_DL_LIBS}
    Threads::Threads
)

add_executable(polyc
    tools/polyc/src/driver.cpp
)

target_link_libraries(polyc
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
    backend_x86_64
    backend_arm64
    backend_wasm
    middle_ir
    runtime
    linker_lib
)

add_executable(polyasm
    tools/polyasm/src/assembler.cpp
)

target_link_libraries(polyasm
    backend_x86_64
    backend_arm64
    backend_wasm
    middle_ir
    polyglot_common
)

# Linker library (for unit tests)
add_library(linker_lib OBJECT
    tools/polyld/src/linker.cpp
    tools/polyld/src/polyglot_linker.cpp
)

target_include_directories(linker_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(linker_lib PUBLIC
    polyglot_common
    frontend_ploy
)

target_compile_definitions(linker_lib PRIVATE LINKER_LIBRARY)

add_executable(polyld
    tools/polyld/src/linker.cpp
    tools/polyld/src/polyglot_linker.cpp
)

target_link_libraries(polyld
    polyglot_common
    frontend_ploy
)

add_executable(polyopt
    tools/polyopt/src/optimizer.cpp
)

target_link_libraries(polyopt
    middle_ir
    backend_x86_64
    backend_arm64
    backend_wasm
    polyglot_common
)

add_executable(polyrt
    tools/polyrt/src/polyrt.cpp
)

target_link_libraries(polyrt
    runtime
    polyglot_common
)

add_executable(polybench
    tools/polybench/src/benchmark_suite.cpp
)

target_link_libraries(polybench
    polyglot_common
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
    middle_ir
    backend_x86_64
    backend_arm64
    backend_wasm
    runtime
    nlohmann_json::nlohmann_json
)

# PolyglotCompiler IDE (Qt-based desktop UI)
# Try Qt6 first, fall back to Qt5.  polyui is only built when either is found.
find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 QUIET COMPONENTS Widgets)
endif()

if(Qt6_FOUND OR Qt5_FOUND)
    set(CMAKE_AUTOMOC ON)

    add_executable(polyui
        tools/ui/src/main.cpp
        tools/ui/src/main_window.cpp
        tools/ui/src/code_editor.cpp
        tools/ui/src/syntax_highlighter.cpp
        tools/ui/src/file_browser.cpp
        tools/ui/src/output_panel.cpp
        tools/ui/src/compiler_service.cpp
        # Headers with Q_OBJECT must be listed for AUTOMOC
        tools/ui/include/main_window.h
        tools/ui/include/code_editor.h
        tools/ui/include/syntax_highlighter.h
        tools/ui/include/file_browser.h
        tools/ui/include/output_panel.h
    )

    target_include_directories(polyui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(Qt6_FOUND)
        set(QT_LIB Qt6::Widgets)
        message(STATUS "Qt6 found — building polyui (IDE)")
    else()
        set(QT_LIB Qt5::Widgets)
        message(STATUS "Qt5 found — building polyui (IDE)")
    endif()

    target_link_libraries(polyui
        ${QT_LIB}
        frontend_python
        frontend_cpp
        frontend_rust
        frontend_ploy
        frontend_java
        frontend_dotnet
        middle_ir
        backend_x86_64
        backend_arm64
        backend_wasm
        runtime
        polyglot_common
        nlohmann_json::nlohmann_json
    )

    # On Windows, mark as a GUI application
    if(WIN32)
        set_target_properties(polyui PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
else()
    message(STATUS "Qt NOT found — skipping polyui (IDE). Install Qt5 or Qt6 to build the IDE.")
endif()

file(GLOB_RECURSE UNIT_TEST_SOURCES tests/unit/*.cpp)
add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE
    polyglot_common
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
    middle_ir
    backend_x86_64
    backend_arm64
    backend_wasm
    runtime
    linker_lib
    Catch2::Catch2
    Catch2::Catch2WithMain
)

# Integration tests (compile pipeline, interop, performance stress)
file(GLOB_RECURSE INTEGRATION_TEST_SOURCES tests/integration/*.cpp)
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests PRIVATE
    polyglot_common
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
    middle_ir
    backend_x86_64
    backend_arm64
    backend_wasm
    runtime
    linker_lib
    Catch2::Catch2
    Catch2::Catch2WithMain
)

# Benchmark tests (micro and macro benchmarks)
file(GLOB_RECURSE BENCHMARK_TEST_SOURCES tests/benchmarks/*.cpp)
add_executable(benchmark_tests ${BENCHMARK_TEST_SOURCES})
target_link_libraries(benchmark_tests PRIVATE
    polyglot_common
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
    middle_ir
    backend_x86_64
    backend_arm64
    backend_wasm
    runtime
    linker_lib
    Catch2::Catch2
    Catch2::Catch2WithMain
)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
add_test(NAME integration_tests COMMAND integration_tests)
add_test(NAME benchmark_tests COMMAND benchmark_tests)

# Benchmark tier targets — controlled by the POLYBENCH_MODE environment variable.
# benchmark_fast  : POLYBENCH_MODE=fast  (1 warmup, reduced iterations)
# benchmark_full  : POLYBENCH_MODE=full  (5 warmup, full iterations)
add_test(NAME benchmark_fast COMMAND ${CMAKE_COMMAND} -E env POLYBENCH_MODE=fast $<TARGET_FILE:benchmark_tests>)
add_test(NAME benchmark_full COMMAND ${CMAKE_COMMAND} -E env POLYBENCH_MODE=full $<TARGET_FILE:benchmark_tests>)

set_tests_properties(benchmark_tests PROPERTIES LABELS "benchmark")
set_tests_properties(benchmark_fast  PROPERTIES LABELS "benchmark;fast")
set_tests_properties(benchmark_full  PROPERTIES LABELS "benchmark;full")
