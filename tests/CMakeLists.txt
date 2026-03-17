# ============================================================================
# tests/ — test targets
# Unit tests, integration tests, and benchmark tests.
# Source files are listed explicitly per module to avoid GLOB_RECURSE and to
# enable per-module rebuild granularity.
# ============================================================================

# -- Common link libraries used by all test targets ----------------------------

set(POLYGLOT_ALL_FRONTENDS
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
)

set(POLYGLOT_ALL_BACKENDS
    backend_x86_64
    backend_arm64
    backend_wasm
)

set(POLYGLOT_TEST_COMMON_LIBS
    ${POLYGLOT_ALL_FRONTENDS}
    ${POLYGLOT_ALL_BACKENDS}
    runtime
    linker_lib
    Catch2::Catch2
    Catch2::Catch2WithMain
)

# ============================================================================
# Unit Tests — per-module source lists
# ============================================================================

# Core / common tests
set(UNIT_TEST_CORE
    unit/type_system_test.cpp
    unit/dwarf5_test.cpp
)

# Plugin tests
set(UNIT_TEST_PLUGINS
    unit/plugins/plugin_manager_test.cpp
)

# Frontend common tests
set(UNIT_TEST_FRONTEND_COMMON
    unit/frontend/preprocessor_tests.cpp
    unit/frontends/frontend_registry_test.cpp
)

# Python frontend tests
set(UNIT_TEST_FRONTEND_PYTHON
    unit/frontends/python/advanced_features_test.cpp
    unit/frontends/python/e2e_python_test.cpp
    unit/frontends/python/import_test.cpp
    unit/frontends/python/lexer_fstring_test.cpp
    unit/frontends/python/lowering_test.cpp
    unit/frontends/python/parser_advanced_test.cpp
    unit/frontends/python/parser_basic_test.cpp
    unit/frontends/python/phi_and_control_flow_test.cpp
    unit/frontends/python/sema_attribute_test.cpp
    unit/frontends/python/sema_scope_test.cpp
)

# C++ frontend tests
set(UNIT_TEST_FRONTEND_CPP
    unit/frontends/cpp/parser_advanced_test.cpp
    unit/frontends/cpp/parser_basic_test.cpp
    unit/frontends/cpp/sema_member_overload_test.cpp
    unit/constexpr_test.cpp
)

# Rust frontend tests
set(UNIT_TEST_FRONTEND_RUST
    unit/frontends/rust/advanced_features_test.cpp
    unit/frontends/rust/borrow_checker_test.cpp
    unit/frontends/rust/lowering_test.cpp
    unit/frontends/rust/parser_advanced_test.cpp
    unit/frontends/rust/parser_basic_test.cpp
    unit/frontends/rust/parser_closure_macro_test.cpp
    unit/frontends/rust/parser_types_traits_test.cpp
    unit/frontends/rust/sema_match_exhaust_test.cpp
)

# Ploy frontend tests
set(UNIT_TEST_FRONTEND_PLOY
    unit/frontends/ploy/ploy_test.cpp
    unit/frontends/ploy/sema_discovery_test.cpp
)

# Java frontend tests
set(UNIT_TEST_FRONTEND_JAVA
    unit/frontends/java/java_test.cpp
)

# .NET frontend tests
set(UNIT_TEST_FRONTEND_DOTNET
    unit/frontends/dotnet/dotnet_test.cpp
)

# Middle (IR / optimization) tests
set(UNIT_TEST_MIDDLE
    unit/middle/ir_roundtrip_test.cpp
    unit/middle/ir_ssa_test.cpp
    unit/middle/lto_test.cpp
    unit/middle/optimization_passes_test.cpp
    unit/middle/template_instantiator_test.cpp
    unit/devirtualization_test.cpp
    unit/gvn_test.cpp
    unit/loop_optimization_test.cpp
)

# Backend tests
set(UNIT_TEST_BACKENDS
    unit/backends/backend_optimizations_test.cpp
    unit/backends/isel_opcodes_test.cpp
)

# Runtime tests
set(UNIT_TEST_RUNTIME
    unit/runtime/ffi_interop_test.cpp
    unit/runtime/gc_algorithms_test.cpp
    unit/runtime/polyrt_test.cpp
    unit/runtime/runtime_tests.cpp
    unit/runtime/threading_services_test.cpp
)

# Linker tests
set(UNIT_TEST_LINKER
    unit/linker_test.cpp
)

# End-to-end tests
set(UNIT_TEST_E2E
    unit/e2e/e2e_compilation_test.cpp
    unit/e2e/compilation_behavior_test.cpp
)

# Combine all unit test sources
set(ALL_UNIT_TEST_SOURCES
    ${UNIT_TEST_CORE}
    ${UNIT_TEST_PLUGINS}
    ${UNIT_TEST_FRONTEND_COMMON}
    ${UNIT_TEST_FRONTEND_PYTHON}
    ${UNIT_TEST_FRONTEND_CPP}
    ${UNIT_TEST_FRONTEND_RUST}
    ${UNIT_TEST_FRONTEND_PLOY}
    ${UNIT_TEST_FRONTEND_JAVA}
    ${UNIT_TEST_FRONTEND_DOTNET}
    ${UNIT_TEST_MIDDLE}
    ${UNIT_TEST_BACKENDS}
    ${UNIT_TEST_RUNTIME}
    ${UNIT_TEST_LINKER}
    ${UNIT_TEST_E2E}
)

add_executable(unit_tests ${ALL_UNIT_TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE ${POLYGLOT_TEST_COMMON_LIBS})

# ============================================================================
# Integration Tests
# ============================================================================

file(GLOB_RECURSE INTEGRATION_TEST_SOURCES integration/*.cpp)
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests PRIVATE ${POLYGLOT_TEST_COMMON_LIBS})

# ============================================================================
# Benchmark Tests
# ============================================================================

file(GLOB_RECURSE BENCHMARK_TEST_SOURCES benchmarks/*.cpp)
add_executable(benchmark_tests ${BENCHMARK_TEST_SOURCES})
target_link_libraries(benchmark_tests PRIVATE ${POLYGLOT_TEST_COMMON_LIBS})

# ============================================================================
# CTest registration
# ============================================================================

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
add_test(NAME integration_tests COMMAND integration_tests)
add_test(NAME benchmark_tests COMMAND benchmark_tests)

# Benchmark tier targets
add_test(NAME benchmark_fast COMMAND ${CMAKE_COMMAND} -E env POLYBENCH_MODE=fast $<TARGET_FILE:benchmark_tests>)
add_test(NAME benchmark_full COMMAND ${CMAKE_COMMAND} -E env POLYBENCH_MODE=full $<TARGET_FILE:benchmark_tests>)

set_tests_properties(benchmark_tests PROPERTIES LABELS "benchmark")
set_tests_properties(benchmark_fast  PROPERTIES LABELS "benchmark;fast")
set_tests_properties(benchmark_full  PROPERTIES LABELS "benchmark;full")
