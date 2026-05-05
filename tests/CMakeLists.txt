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
    frontend_javascript
    frontend_ruby
    frontend_go
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
    polyglot_tools_settings
    ${CATCH2_LIBS}
)

# ============================================================================
# Per-module source lists (unchanged from before)
# ============================================================================

set(UNIT_TEST_CORE
    unit/type_system_test.cpp
    unit/dwarf5_test.cpp
    unit/target_triple_test.cpp
)

set(UNIT_TEST_PLUGINS
    unit/plugins/plugin_manager_test.cpp
)

set(UNIT_TEST_FRONTEND_COMMON
    unit/frontend/preprocessor_tests.cpp
    unit/frontend/token_pool_test.cpp
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
    unit/frontends/python/version_gating_test.cpp
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
    unit/frontends/ploy/sema_strict_mode_test.cpp
    unit/frontends/ploy/lang_version_pin_test.cpp
    unit/frontends/ploy/lexer_case_insensitive_test.cpp
    unit/frontends/ploy/keyword_alias_test.cpp
    unit/frontends/ploy/println_stmt_test.cpp
    unit/frontends/ploy/println_lowering_test.cpp
    unit/frontends/ploy/synthetic_main_test.cpp
    unit/frontends/ploy/type_alias_test.cpp
    unit/frontends/ploy/const_decl_test.cpp
    unit/frontends/ploy/width_mismatch_diag_test.cpp
    unit/frontends/ploy/link_deprecation_test.cpp
    unit/frontends/ploy/stage_misuse_test.cpp
    unit/frontends/ploy/typed_handle_test.cpp
    unit/frontends/ploy/pattern_matching_test.cpp
    unit/frontends/ploy/pattern_matching_lowering_test.cpp
    unit/frontends/ploy/default_args_and_extend_test.cpp
    unit/frontends/ploy/config_stringification_test.cpp
    unit/frontends/ploy/try_catch_throw_test.cpp
    unit/frontends/ploy/async_await_test.cpp
    unit/frontends/ploy/generics_test.cpp
    unit/frontends/ploy/visibility_attrs_test.cpp
    unit/frontends/ploy/string_literals_test.cpp
    unit/frontends/ploy/polish_grammar_test.cpp
)

set(UNIT_TEST_FRONTEND_JAVA
    unit/frontends/java/java_test.cpp
)

set(UNIT_TEST_FRONTEND_DOTNET
    unit/frontends/dotnet/dotnet_test.cpp
)

set(UNIT_TEST_FRONTEND_JAVASCRIPT
    unit/frontends/javascript/javascript_test.cpp
)

set(UNIT_TEST_FRONTEND_RUBY
    unit/frontends/ruby/ruby_test.cpp
)

set(UNIT_TEST_FRONTEND_GO
    unit/frontends/go/go_test.cpp
)

set(UNIT_TEST_MIDDLE
    unit/middle/ir_roundtrip_test.cpp
    unit/middle/ir_ssa_test.cpp
    unit/middle/lto_test.cpp
    unit/middle/optimization_passes_test.cpp
    unit/middle/opt_level_regression_test.cpp
    unit/middle/template_instantiator_test.cpp
    unit/devirtualization_test.cpp
    unit/gvn_test.cpp
    unit/loop_optimization_test.cpp
)

set(UNIT_TEST_BACKENDS
    unit/backends/abi_calling_convention_test.cpp
    unit/backends/abi_relocation_test.cpp
    unit/backends/abi_verifier_test.cpp
    unit/backends/backend_optimizations_test.cpp
    unit/backends/isel_opcodes_test.cpp
    unit/backends/machine_ir_template_test.cpp
    unit/backends/machine_ir_verifier_test.cpp
    unit/backends/target_backend_registry_test.cpp
    unit/backends/wasm_split_smoke_test.cpp
    unit/backends/emit_bitcode_roundtrip_test.cpp
    unit/backends/debug_emitter_normalization_test.cpp
    unit/backends/wasm_target_test.cpp
    unit/backends/coff_builder_test.cpp
    unit/backends/globals_emit_test.cpp
    unit/backends/printf_lea_reloc_test.cpp
)

set(UNIT_TEST_RUNTIME
    unit/runtime/ffi_interop_test.cpp
    unit/runtime/gc_algorithms_test.cpp
    unit/runtime/polyrt_test.cpp
    unit/runtime/runtime_tests.cpp
    unit/runtime/threading_services_test.cpp
    unit/runtime/call_trace_runtime_test.cpp
    unit/runtime/error_bridge_test.cpp
    unit/runtime/async_bridge_test.cpp
)

set(UNIT_TEST_LINKER
    unit/linker_test.cpp
    unit/linker/pe_writer_test.cpp
    unit/linker/pe_writer_hardened_test.cpp
    unit/linker/linker_pe_test.cpp
    unit/linker/object_format_detect_test.cpp
    unit/linker/section_merge_test.cpp
    unit/linker/polyrt_println_collect_test.cpp
    unit/linker/output_dispatch_test.cpp
)

set(UNIT_TEST_TOPOLOGY
    unit/tools/topology_test.cpp
    unit/tools/linker_probe_test.cpp
)

set(UNIT_TEST_SETTINGS
    unit/tools/settings_loader_test.cpp
)

set(UNIT_TEST_LSP
    unit/polyui/lsp_client_test.cpp
)

set(UNIT_TEST_PROBLEMS
    unit/polyui/problems_panel_model_test.cpp
)

set(UNIT_TEST_POLYLS
    unit/polyls/lifecycle_test.cpp
    unit/polyls/completion_test.cpp
    unit/polyls/hover_test.cpp
    unit/polyls/signature_help_test.cpp
    unit/polyls/symbol_index_test.cpp
    unit/polyls/navigation_test.cpp
    unit/polyls/refactor_test.cpp
    unit/polyls/tree_sitter_runtime_test.cpp
    unit/polyls/semantic_tokens_test.cpp
    unit/polyls/workspace_symbol_test.cpp
    unit/polyls/formatting_test.cpp
)

set(UNIT_TEST_COMPLETION_RANKER
    unit/polyui/completion_ranker_test.cpp
)

set(UNIT_TEST_POLYUI_PANELS
    unit/polyui/editor_group_test.cpp
    unit/polyui/quick_open_ranker_test.cpp
    unit/polyui/global_search_engine_test.cpp
    unit/polyui/outline_model_test.cpp
    unit/polyui/multi_cursor_test.cpp
    unit/polyui/folding_model_test.cpp
    unit/polyui/format_engine_test.cpp
    unit/polyui/snippet_engine_test.cpp
    unit/polyui/editor_config_test.cpp
    unit/polyui/diff_engine_test.cpp
    unit/polyui/merge_resolver_test.cpp
    unit/polyui/scm_provider_test.cpp
    unit/polyui/git_provider_test.cpp
    unit/polyui/dap_client_test.cpp
    unit/polyui/launch_config_test.cpp
    unit/polyui/debug_session_test.cpp
    unit/polyui/task_config_test.cpp
    unit/polyui/problem_matcher_test.cpp
    unit/polyui/task_runner_test.cpp
    unit/polyui/run_debug_picker_test.cpp
    unit/polyui/hot_reload_test.cpp
    unit/polyui/test_model_test.cpp
    unit/polyui/inline_test_lens_test.cpp
    unit/polyui/coverage_model_test.cpp
    unit/polyui/package_manager_test.cpp
    unit/polyui/dependency_graph_test.cpp
    unit/polyui/vulnerability_scanner_test.cpp
    unit/polyui/notebook_test.cpp
    unit/polyui/cross_language_navigator_test.cpp
    unit/polyui/bridge_panel_test.cpp
    unit/polyui/marshalling_view_test.cpp
    unit/polyui/pipeline_inspector_test.cpp
    unit/polyui/ir_viewer_test.cpp
    unit/polyui/asm_viewer_test.cpp
    unit/polyui/sample_browser_test.cpp
    unit/polyui/topology_live_test.cpp
    unit/polyui/inlay_hints_test.cpp
    unit/polyui/remote_session_test.cpp
    unit/polyui/dev_container_test.cpp
    unit/polyui/file_sync_test.cpp
    unit/polyui/ai_provider_test.cpp
    unit/polyui/inline_suggestion_test.cpp
    unit/polyui/refactor_diff_test.cpp
    unit/polyui/collab_provider_test.cpp
    unit/polyui/extension_api_test.cpp
    unit/polyui/marketplace_test.cpp
    unit/polyui/workspace_test.cpp
    unit/polyui/shell_test.cpp
    unit/polyui/localization_test.cpp
    unit/polyui/viewers_test.cpp
)

set(UNIT_TEST_TOPOLOGY_UI
    unit/tools/topology_ui_main.cpp
    unit/tools/drilldown_window_test.cpp
    unit/tools/topology_ui_extended_test.cpp
    unit/tools/topology_layout_test.cpp
    unit/tools/call_graph_model_test.cpp
    unit/tools/profile_session_test.cpp
    unit/tools/profiler_panel_smoke_test.cpp
)

set(UNIT_TEST_E2E
    unit/e2e/e2e_compilation_test.cpp
    unit/e2e/compilation_behavior_test.cpp
    unit/e2e/polyc_cli_e2e_test.cpp
    unit/polyc/section_passthrough_test.cpp
    unit/e2e/expansion_features_test.cpp
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

# -- JavaScript frontend -----------------------------------------------------
add_executable(test_frontend_javascript ${UNIT_TEST_FRONTEND_JAVASCRIPT})
target_link_libraries(test_frontend_javascript PRIVATE
    frontend_javascript ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- Ruby frontend -----------------------------------------------------------
add_executable(test_frontend_ruby ${UNIT_TEST_FRONTEND_RUBY})
target_link_libraries(test_frontend_ruby PRIVATE
    frontend_ruby ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

# -- Go frontend -------------------------------------------------------------
add_executable(test_frontend_go ${UNIT_TEST_FRONTEND_GO})
target_link_libraries(test_frontend_go PRIVATE
    frontend_go ${POLYGLOT_ALL_BACKENDS} runtime ${CATCH2_LIBS})

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
    linker_lib frontend_ploy backend_common ${CATCH2_LIBS})

# -- Topology ----------------------------------------------------------------
add_executable(test_topology ${UNIT_TEST_TOPOLOGY})
target_link_libraries(test_topology PRIVATE
    topo_lib polyc_lib frontend_ploy frontend_cpp frontend_python ${CATCH2_LIBS})

# -- Settings (3-layer JSON loader, schema validation) -----------------------
add_executable(test_settings ${UNIT_TEST_SETTINGS})
target_include_directories(test_settings PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_settings PRIVATE
    polyglot_tools_settings ${CATCH2_LIBS})

# -- LSP client core (Qt-free) -----------------------------------------------
add_executable(test_lsp ${UNIT_TEST_LSP})
target_include_directories(test_lsp PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_lsp PRIVATE
    lsp_lib ${CATCH2_LIBS})

# -- Problems aggregator (Qt-free) -------------------------------------------
# Builds the aggregator translation unit straight into the test binary so we
# don't have to depend on the Qt-heavy polyui executable target.  The
# aggregator only needs DiagnosticInfo from compiler_service.h plus the
# core::SourceLoc type from polyglot_common (transitively via
# frontends/common/include/diagnostics.h).
add_executable(test_problems
    ${UNIT_TEST_PROBLEMS}
    ${CMAKE_SOURCE_DIR}/tools/ui/common/src/problems_aggregator.cpp
)
target_include_directories(test_problems PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_problems PRIVATE
    polyglot_common ${CATCH2_LIBS})

# -- polyls (server-side lifecycle / sync / diagnostics) ---------------------
add_executable(test_polyls ${UNIT_TEST_POLYLS})
target_include_directories(test_polyls PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_polyls PRIVATE
    polyls_core ${CATCH2_LIBS})

# -- Completion ranker (Qt-free helper) --------------------------------------
add_executable(test_completion_ranker
    ${UNIT_TEST_COMPLETION_RANKER}
    ${CMAKE_SOURCE_DIR}/tools/ui/common/src/completion_ranker.cpp
)
target_include_directories(test_completion_ranker PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_completion_ranker PRIVATE ${CATCH2_LIBS})

# -- Topology UI (requires Qt) -----------------------------------------------
# DrillDownWindow / BreadcrumbBar tests need a QApplication and Qt widgets.
find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 QUIET COMPONENTS Widgets)
endif()
if(Qt6_FOUND OR Qt5_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(test_topology_ui ${UNIT_TEST_TOPOLOGY_UI}
        ${CMAKE_SOURCE_DIR}/tools/ui/common/src/topology_panel.cpp
        ${CMAKE_SOURCE_DIR}/tools/ui/common/include/topology_panel.h
        ${CMAKE_SOURCE_DIR}/tools/ui/common/src/profiler_panel.cpp
        ${CMAKE_SOURCE_DIR}/tools/ui/common/include/profiler_panel.h
        ${CMAKE_SOURCE_DIR}/tools/ui/common/src/call_analyzer_panel.cpp
        ${CMAKE_SOURCE_DIR}/tools/ui/common/include/call_analyzer_panel.h
        ${CMAKE_SOURCE_DIR}/tools/ui/common/src/profile_session.cpp
        ${CMAKE_SOURCE_DIR}/tools/ui/common/include/profile_session.h
        ${CMAKE_SOURCE_DIR}/tools/ui/common/src/data_models/flame_node.cpp
        ${CMAKE_SOURCE_DIR}/tools/ui/common/include/data_models/flame_node.h
        ${CMAKE_SOURCE_DIR}/tools/ui/common/src/data_models/call_graph_model.cpp
        ${CMAKE_SOURCE_DIR}/tools/ui/common/include/data_models/call_graph_model.h
        ${CMAKE_SOURCE_DIR}/tools/ui/common/src/data_models/timeline_model.cpp
        ${CMAKE_SOURCE_DIR}/tools/ui/common/include/data_models/timeline_model.h
        ${CMAKE_SOURCE_DIR}/tools/polyc/src/foreign_signature_extractor.cpp
    )
    target_include_directories(test_topology_ui PRIVATE ${CMAKE_SOURCE_DIR})
    if(Qt6_FOUND)
        set(_QT_UI_TEST_LIB Qt6::Widgets)
    else()
        set(_QT_UI_TEST_LIB Qt5::Widgets)
    endif()
    target_link_libraries(test_topology_ui PRIVATE
        ${_QT_UI_TEST_LIB}
        topo_lib polyc_lib
        frontend_ploy frontend_cpp frontend_python
        Catch2::Catch2)
    message(STATUS "Qt found — building test_topology_ui (DrillDownWindow tests)")

    # macOS: embed an rpath pointing to the Qt frameworks directory so dyld can
    # locate QtWidgets.framework etc. without DYLD_FRAMEWORK_PATH being set.
    if(APPLE)
        if(Qt6_FOUND)
            get_target_property(_qt_test_widgets_loc Qt6::Widgets LOCATION)
        else()
            get_target_property(_qt_test_widgets_loc Qt5::Widgets LOCATION)
        endif()
        if(_qt_test_widgets_loc)
            # Frameworks live at <dir>/QtWidgets.framework/Versions/A/QtWidgets,
            # so the rpath the binary needs is <dir>.
            get_filename_component(_qt_fw_versions "${_qt_test_widgets_loc}" DIRECTORY)
            get_filename_component(_qt_fw_a       "${_qt_fw_versions}"      DIRECTORY)
            get_filename_component(_qt_fw_root    "${_qt_fw_a}"             DIRECTORY)
            get_filename_component(_qt_fw_dir     "${_qt_fw_root}"          DIRECTORY)
            # The project sets CMAKE_BUILD_WITH_INSTALL_RPATH=TRUE, so the
            # build-tree binary uses INSTALL_RPATH directly. Append the Qt
            # frameworks dir to the existing @executable_path-based entries
            # rather than overwriting them.
            set_property(TARGET test_topology_ui APPEND PROPERTY
                INSTALL_RPATH "${_qt_fw_dir}"
            )
        endif()
    endif()

    # Windows: deploy Qt runtime DLLs next to test_topology_ui.exe so that
    # CTest can run the test without manual PATH configuration.
    if(WIN32)
        if(Qt6_FOUND)
            get_target_property(_qt_test_qmake Qt6::qmake IMPORTED_LOCATION)
        else()
            get_target_property(_qt_test_qmake Qt5::qmake IMPORTED_LOCATION)
        endif()
        get_filename_component(_qt_test_bin_dir "${_qt_test_qmake}" DIRECTORY)

        find_program(_WINDEPLOYQT_TEST windeployqt
            HINTS "${_qt_test_bin_dir}" NO_DEFAULT_PATH)
        if(_WINDEPLOYQT_TEST)
            add_custom_command(TARGET test_topology_ui POST_BUILD
                COMMAND "${_WINDEPLOYQT_TEST}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-compiler-runtime
                    "$<TARGET_FILE:test_topology_ui>"
                COMMENT "Running windeployqt for test_topology_ui..."
                VERBATIM
            )
            message(STATUS "windeployqt will deploy Qt DLLs for test_topology_ui")
        else()
            message(WARNING "windeployqt not found — test_topology_ui may fail at runtime")
        endif()
    endif()
endif()

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
    ${UNIT_TEST_FRONTEND_JAVASCRIPT}
    ${UNIT_TEST_FRONTEND_RUBY}
    ${UNIT_TEST_FRONTEND_GO}
    ${UNIT_TEST_MIDDLE}
    ${UNIT_TEST_BACKENDS}
    ${UNIT_TEST_RUNTIME}
    ${UNIT_TEST_LINKER}
    ${UNIT_TEST_TOPOLOGY}
    ${UNIT_TEST_SETTINGS}
    ${UNIT_TEST_LSP}
    ${UNIT_TEST_POLYLS}
    ${UNIT_TEST_PROBLEMS}
    ${UNIT_TEST_COMPLETION_RANKER}
    ${UNIT_TEST_POLYUI_PANELS}
    ${UNIT_TEST_E2E}
)

add_executable(unit_tests ${ALL_UNIT_TEST_SOURCES}
    ${CMAKE_SOURCE_DIR}/tools/ui/common/src/problems_aggregator.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/src/completion_ranker.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/editor/editor_group.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/editor/editor_grid.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/quickopen/quick_open_ranker.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/search/global_search_engine.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/outline/outline_model.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/editing/multi_cursor.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/editing/folding_model.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/editing/format_engine.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/editing/snippet_engine.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/editing/editor_config.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/scm/scm_provider.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/scm/git_provider.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/scm/diff_engine.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/scm/merge_resolver.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/dap/dap_client.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/dap/launch_config.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/dap/debug_session.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/tasks/task_config.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/tasks/problem_matcher.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/tasks/task_runner.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/runtime/run_debug_picker.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/runtime/hot_reload.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/testing/test_model.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/testing/inline_test_lens.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/testing/coverage_model.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/packages/package_manager.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/packages/dependency_graph.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/packages/vulnerability_scanner.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/notebook/repl_session.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/notebook/notebook.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/cross_language/cross_language_navigator.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/cross_language/bridge_panel.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/cross_language/marshalling_view.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/pipeline/pipeline_inspector.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/pipeline/ir_viewer.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/pipeline/asm_viewer.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/samples/sample_browser.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/topology_live/topology_live.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/inlay_hints/inlay_hints.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/remote/remote_session.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/remote/dev_container.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/remote/file_sync.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/ai/ai_provider.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/ai/inline_suggestion.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/ai/refactor_diff.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/collab/collab_provider.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/ext/extension_api.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/ext/marketplace.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/workspace/workspace.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/shell/welcome.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/shell/notifications.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/shell/status_bar.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/shell/recent.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/shell/session.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/shell/bookmarks.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/shell/todo_index.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/i18n/i18n.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/a11y/accessibility.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/telemetry/telemetry.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/viewer/image_viewer.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/viewer/hex_viewer.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/viewer/binary_inspector.cpp
    ${CMAKE_SOURCE_DIR}/tools/ui/common/dbclient/sql_console.cpp)
target_link_libraries(unit_tests PRIVATE ${POLYGLOT_TEST_ALL_LIBS} lsp_lib polyls_core)

# ============================================================================
# Integration Tests
# ============================================================================

set(INTEGRATION_TEST_SOURCES
    integration/compile_tests/compile_pipeline_test.cpp
    integration/interop_tests/interop_test.cpp
    integration/performance/perf_test.cpp
    integration/e2e/polyc_e2e_test.cpp
    integration/object_format_test.cpp
)
set(INTEGRATION_TEST_SOURCES
    integration/compile_tests/compile_pipeline_test.cpp
    integration/interop_tests/interop_test.cpp
    integration/performance/perf_test.cpp
    integration/e2e/polyc_e2e_test.cpp
    integration/object_format_test.cpp
    integration/external_packages/external_packages_test.cpp
    integration/external_packages/demand_03_test.cpp
    integration/language_versions/lang_versions_test.cpp
    integration/ui/theme_system_test.cpp
    integration/pe_runtime_smoke_test.cpp
    integration/ploy_e2e_real_exit_code_test.cpp
    integration/samples_regression_test.cpp
    integration/profiler_e2e_test.cpp
    integration/explicit_widths_e2e_test.cpp
    integration/printf_pipeline_e2e_test.cpp
    integration/lsp_diagnostics_e2e_test.cpp
    integration/lsp_navigation_e2e_test.cpp
    integration/lsp_refactor_e2e_test.cpp
    integration/lsp_semantic_tokens_e2e_test.cpp
)
add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
target_link_libraries(integration_tests PRIVATE ${POLYGLOT_TEST_ALL_LIBS} lsp_lib polyls_core)
target_compile_definitions(integration_tests PRIVATE
    POLYGLOT_TESTS_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures/external_packages"
    POLYGLOT_TESTS_SAMPLES_ROOT="${CMAKE_SOURCE_DIR}/tests/samples")

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
add_test(NAME test_frontend_javascript COMMAND test_frontend_javascript)
add_test(NAME test_frontend_ruby     COMMAND test_frontend_ruby)
add_test(NAME test_frontend_go       COMMAND test_frontend_go)
add_test(NAME test_middle            COMMAND test_middle)
add_test(NAME test_backends          COMMAND test_backends)
add_test(NAME test_runtime           COMMAND test_runtime)
add_test(NAME test_linker            COMMAND test_linker)
add_test(NAME test_topology          COMMAND test_topology)
add_test(NAME test_settings          COMMAND test_settings)
add_test(NAME test_lsp               COMMAND test_lsp)
add_test(NAME test_polyls            COMMAND test_polyls)
add_test(NAME test_problems          COMMAND test_problems)
add_test(NAME test_completion_ranker COMMAND test_completion_ranker)
if(TARGET test_topology_ui)
    add_test(NAME test_topology_ui   COMMAND test_topology_ui)
endif()
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
set_tests_properties(test_settings PROPERTIES LABELS "unit;tools;settings")
set_tests_properties(test_lsp PROPERTIES LABELS "unit;polyui;lsp")
set_tests_properties(test_polyls PROPERTIES LABELS "unit;polyls;lsp")
set_tests_properties(test_problems PROPERTIES LABELS "unit;polyui;problems")
set_tests_properties(test_completion_ranker PROPERTIES LABELS "unit;polyui;completion")
if(TARGET test_topology_ui)
    set_tests_properties(test_topology_ui PROPERTIES LABELS "unit;topology;ui")
endif()
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
