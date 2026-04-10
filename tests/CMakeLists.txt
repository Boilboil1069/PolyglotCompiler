# ============================================================================
# tests/ — test targets
# Per-module test binaries to isolate failures and reduce link overhead.
# A combined unit_tests target is kept for backward compatibility.
# ============================================================================

# -- Catch2 link shorthand ---------------------------------------------------
set(CATCH2_LIBS Catch2::Catch2 Catch2::Catch2WithMain)

# -- Frontends / backends / runtime lists ------------------------------------
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

# Full link set — only used by targets that genuinely need everything
set(POLYGLOT_TEST_ALL_LIBS
    ${POLYGLOT_ALL_FRONTENDS}
    ${POLYGLOT_ALL_BACKENDS}
    runtime
    linker_lib
    topo_lib
    polyc_lib
    ${CATCH2_LIBS}
)

# ============================================================================
# Per-module source lists (unchanged from before)
# ============================================================================

set(UNIT_TEST_CORE
    unit/type_system_test.cpp
    unit/dwarf5_test.cpp
)

set(UNIT_TEST_PLUGINS
    unit/plugins/plugin_manager_test.cpp
)

set(UNIT_TEST_FRONTEND_COMMON
    unit/frontend/preprocessor_tests.cpp
    unit/frontends/frontend_registry_test.cpp
)

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

set(UNIT_TEST_FRONTEND_CPP
    unit/frontends/cpp/parser_advanced_test.cpp
    unit/frontends/cpp/parser_basic_test.cpp
    unit/frontends/cpp/sema_member_overload_test.cpp
    unit/constexpr_test.cpp
)

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

set(UNIT_TEST_FRONTEND_PLOY
    unit/frontends/ploy/ploy_test.cpp
    unit/frontends/ploy/sema_discovery_test.cpp
    unit/frontends/ploy/devirtualization_test.cpp
    unit/frontends/ploy/loop_optimization_test.cpp
)

set(UNIT_TEST_FRONTEND_JAVA
    unit/frontends/java/java_test.cpp
)

set(UNIT_TEST_FRONTEND_DOTNET
    unit/frontends/dotnet/dotnet_test.cpp
)

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

set(UNIT_TEST_BACKENDS
    unit/backends/backend_optimizations_test.cpp
    unit/backends/isel_opcodes_test.cpp
)

set(UNIT_TEST_RUNTIME
    unit/runtime/ffi_interop_test.cpp
    unit/runtime/gc_algorithms_test.cpp
    unit/runtime/polyrt_test.cpp
    unit/runtime/runtime_tests.cpp
    unit/runtime/threading_services_test.cpp
)

set(UNIT_TEST_LINKER
    unit/linker_test.cpp
)

set(UNIT_TEST_TOPOLOGY
    unit/tools/topology_test.cpp
)

set(UNIT_TEST_E2E
    unit/e2e/e2e_compilation_test.cpp
    unit/e2e/compilation_behavior_test.cpp
)

# ============================================================================
# Per-module test binaries
# Each binary links only the libraries it actually needs.
# ============================================================================

# Helper macro: define a test binary with specific link libraries
macro(polyglot_add_test TARGET SOURCES LIBS)
    add_executable(${TARGET} ${${SOURCES}})
    target_link_libraries(${TARGET} PRIVATE ${${LIBS}} ${CATCH2_LIBS})
endmacro()

# -- Core / common -----------------------------------------------------------
add_executable(test_core ${UNIT_TEST_CORE})
target_link_libraries(test_core PRIVATE polyglot_common ${CATCH2_LIBS})

# -- Plugins -----------------------------------------------------------------
add_executable(test_plugins ${UNIT_TEST_PLUGINS})
target_link_libraries(test_plugins PRIVATE polyglot_common ${CATCH2_LIBS})

# -- Frontend common ---------------------------------------------------------
add_executable(test_frontend_common ${UNIT_TEST_FRONTEND_COMMON})
target_link_libraries(test_frontend_common PRIVATE
    ${POLYGLOT_ALL_FRONTENDS} ${CATCH2_LIBS})

# -- Python frontend ---------------------------------------------------------
add_executable(test_frontend_python ${UNIT_TEST_FRONTEND_PYTHON})
target_link_libraries(test_frontend_python PRIVATE
    frontend_python ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- C++ frontend ------------------------------------------------------------
add_executable(test_frontend_cpp ${UNIT_TEST_FRONTEND_CPP})
target_link_libraries(test_frontend_cpp PRIVATE
    frontend_cpp ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- Rust frontend -----------------------------------------------------------
add_executable(test_frontend_rust ${UNIT_TEST_FRONTEND_RUST})
target_link_libraries(test_frontend_rust PRIVATE
    frontend_rust ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- Ploy frontend -----------------------------------------------------------
add_executable(test_frontend_ploy ${UNIT_TEST_FRONTEND_PLOY})
target_link_libraries(test_frontend_ploy PRIVATE
    frontend_ploy ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- Java frontend -----------------------------------------------------------
add_executable(test_frontend_java ${UNIT_TEST_FRONTEND_JAVA})
target_link_libraries(test_frontend_java PRIVATE
    frontend_java ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- .NET frontend -----------------------------------------------------------
add_executable(test_frontend_dotnet ${UNIT_TEST_FRONTEND_DOTNET})
target_link_libraries(test_frontend_dotnet PRIVATE
    frontend_dotnet ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- Middle (IR / optimization) ----------------------------------------------
add_executable(test_middle ${UNIT_TEST_MIDDLE})
target_link_libraries(test_middle PRIVATE
    ${POLYGLOT_ALL_FRONTENDS} ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- Backends ----------------------------------------------------------------
add_executable(test_backends ${UNIT_TEST_BACKENDS})
target_link_libraries(test_backends PRIVATE
    ${POLYGLOT_ALL_BACKENDS} ${CATCH2_LIBS})

# -- Runtime -----------------------------------------------------------------
add_executable(test_runtime ${UNIT_TEST_RUNTIME})
target_link_libraries(test_runtime PRIVATE
    runtime ${POLYGLOT_ALL_BACKENDS} ${CATCH2_LIBS})

# -- Linker ------------------------------------------------------------------
add_executable(test_linker ${UNIT_TEST_LINKER})
target_link_libraries(test_linker PRIVATE
    linker_lib frontend_ploy ${CATCH2_LIBS})

# -- Topology ----------------------------------------------------------------
add_executable(test_topology ${UNIT_TEST_TOPOLOGY})
target_link_libraries(test_topology PRIVATE
    topo_lib polyc_lib frontend_ploy frontend_cpp frontend_python ${CATCH2_LIBS})

# -- E2E ---------------------------------------------------------------------
add_executable(test_e2e ${UNIT_TEST_E2E})
target_link_libraries(test_e2e PRIVATE ${POLYGLOT_TEST_ALL_LIBS})

# ============================================================================
# Combined unit_tests target (backward compatibility)
# ============================================================================

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
    ${UNIT_TEST_TOPOLOGY}
    ${UNIT_TEST_E2E}
)

add_executable(unit_tests ${ALL_UNIT_TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE ${POLYGLOT_TEST_ALL_LIBS})

# ============================================================================
# Integration Tests
# ============================================================================

set(INTEGRATION_TEST_SOURCES
    integration/compile_tests/compile_pipeline_test.cpp
    integration/interop_tests/interop_test.cpp
    integration/performance/perf_test.cpp
    integration/e2e/polyc_e2e_test.cpp
)
set(INTEGRATION_TEST_SOURCES
    integration/compile_tests/compile_pipeline_test.cpp
    integration/interop_tests/interop_test.cpp
    integration/performance/perf_test.cpp
    integration/e2e/polyc_e2e_test.cpp
)
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests PRIVATE ${POLYGLOT_TEST_ALL_LIBS})

# ============================================================================
# Benchmark Tests
# ============================================================================

file(GLOB_RECURSE BENCHMARK_TEST_SOURCES benchmarks/*.cpp)
add_executable(benchmark_tests ${BENCHMARK_TEST_SOURCES})
target_link_libraries(benchmark_tests PRIVATE ${POLYGLOT_TEST_ALL_LIBS})

# ============================================================================
# CTest registration
# ============================================================================

enable_testing()

# Per-module unit tests
add_test(NAME test_core              COMMAND test_core)
add_test(NAME test_plugins           COMMAND test_plugins)
add_test(NAME test_frontend_common   COMMAND test_frontend_common)
add_test(NAME test_frontend_python   COMMAND test_frontend_python)
add_test(NAME test_frontend_cpp      COMMAND test_frontend_cpp)
add_test(NAME test_frontend_rust     COMMAND test_frontend_rust)
add_test(NAME test_frontend_ploy     COMMAND test_frontend_ploy)
add_test(NAME test_frontend_java     COMMAND test_frontend_java)
add_test(NAME test_frontend_dotnet   COMMAND test_frontend_dotnet)
add_test(NAME test_middle            COMMAND test_middle)
add_test(NAME test_backends          COMMAND test_backends)
add_test(NAME test_runtime           COMMAND test_runtime)
add_test(NAME test_linker            COMMAND test_linker)
add_test(NAME test_topology          COMMAND test_topology)
add_test(NAME test_e2e               COMMAND test_e2e)

# Labels for per-module tests
set_tests_properties(test_core test_plugins PROPERTIES LABELS "unit;core")
set_tests_properties(test_frontend_common test_frontend_python test_frontend_cpp
    test_frontend_rust test_frontend_ploy test_frontend_java test_frontend_dotnet
    PROPERTIES LABELS "unit;frontend")
set_tests_properties(test_middle PROPERTIES LABELS "unit;middle")
set_tests_properties(test_backends PROPERTIES LABELS "unit;backend")
set_tests_properties(test_runtime PROPERTIES LABELS "unit;runtime")
set_tests_properties(test_linker PROPERTIES LABELS "unit;linker")
set_tests_properties(test_topology PROPERTIES LABELS "unit;topology")
set_tests_properties(test_e2e PROPERTIES LABELS "unit;e2e")

# Combined targets (backward compatibility)
add_test(NAME unit_tests        COMMAND unit_tests)
add_test(NAME integration_tests COMMAND integration_tests)
add_test(NAME benchmark_tests   COMMAND benchmark_tests)

# Benchmark tier targets
add_test(NAME benchmark_fast COMMAND ${CMAKE_COMMAND} -E env POLYBENCH_MODE=fast $<TARGET_FILE:benchmark_tests>)
add_test(NAME benchmark_full COMMAND ${CMAKE_COMMAND} -E env POLYBENCH_MODE=full $<TARGET_FILE:benchmark_tests>)

set_tests_properties(benchmark_tests PROPERTIES LABELS "benchmark")
set_tests_properties(benchmark_fast  PROPERTIES LABELS "benchmark;fast")
set_tests_properties(benchmark_full  PROPERTIES LABELS "benchmark;full")
