# ============================================================================
# tools/ — executable targets
# polyc, polyasm, polyld, polyopt, polyrt, polybench, and polyui (Qt IDE).
# ============================================================================

# -- polyglot_tools_settings (shared CLI/UI settings loader) -------------------
# Implements the 3-layer (default/user/workspace) settings.json loader used
# by every CLI tool and by the polyui IDE.  Header-only public API +
# nlohmann/json.  See tools/common/include/effective_settings_loader.h.

add_library(polyglot_tools_settings STATIC
    ../tools/common/src/effective_settings_loader.cpp
)
target_include_directories(polyglot_tools_settings PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(polyglot_tools_settings PUBLIC nlohmann_json::nlohmann_json)

# -- polyc_lib (shared pipeline library — used by polyc and integration tests) -

add_library(polyc_lib STATIC
    polyc/src/compilation_pipeline.cpp
    polyc/src/stage_frontend.cpp
    polyc/src/stage_semantic.cpp
    polyc/src/stage_marshal.cpp
    polyc/src/stage_bridge.cpp
    polyc/src/stage_backend.cpp
    polyc/src/stage_packaging.cpp
    polyc/src/foreign_signature_extractor.cpp
    polyc/src/linker_probe.cpp
    polyc/src/call_graph_emitter.cpp
)

target_include_directories(polyc_lib PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(polyc_lib PUBLIC
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
    frontend_javascript
    frontend_ruby
    frontend_go
    backend_x86_64
    backend_arm64
    backend_wasm
    runtime
    linker_lib
)

# -- polyc (compiler driver) ---------------------------------------------------

add_executable(polyc
    polyc/src/driver.cpp
)

target_link_libraries(polyc
    polyc_lib
    polyglot_tools_settings
)

# Enable strict mode by default for Release builds
target_compile_definitions(polyc PRIVATE
    $<$<CONFIG:Release>:POLYC_DEFAULT_STRICT>
)

# -- polyasm (assembler) -------------------------------------------------------

add_executable(polyasm
    polyasm/src/assembler.cpp
)

target_link_libraries(polyasm
    backend_x86_64
    backend_arm64
    backend_wasm
    middle_ir
)

# -- linker_lib (shared between polyld and unit tests) -------------------------

add_library(linker_lib
    polyld/src/linker.cpp
    polyld/src/polyglot_linker.cpp
    polyld/src/pe_writer.cpp
)

target_include_directories(linker_lib PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(linker_lib PUBLIC
    polyglot_common
    frontend_ploy
)

target_compile_definitions(linker_lib PRIVATE LINKER_LIBRARY)

# -- polyld (linker) -----------------------------------------------------------

add_executable(polyld
    polyld/src/main.cpp
)

target_link_libraries(polyld
    linker_lib
    polyglot_common
    frontend_ploy
    polyglot_tools_settings
)

# -- pe_smoke (throwaway PE writer harness, only useful on Windows hosts) ------

add_executable(pe_smoke
    polyld/src/pe_writer_smoke.cpp
)
target_link_libraries(pe_smoke linker_lib polyglot_common)

# -- polyopt (standalone optimizer) --------------------------------------------

add_executable(polyopt
    polyopt/src/optimizer.cpp
)

target_link_libraries(polyopt
    middle_ir
    backend_x86_64
    backend_arm64
    backend_wasm
)

# -- polyrt (runtime launcher) -------------------------------------------------

add_executable(polyrt
    polyrt/src/polyrt.cpp
)

target_link_libraries(polyrt
    runtime
    polyglot_common
    polyglot_tools_settings
)

# -- polyver (multi-language tool-chain manager) -------------------------------
# Introduced by demand 2026-04-27-3.

add_executable(polyver
    polyver/src/polyver.cpp
    polyver/src/toolchain_db.cpp
)

target_include_directories(polyver PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(polyver
    polyglot_common
    nlohmann_json::nlohmann_json
)

# -- polydoc (.ploy doc-comment extractor) -------------------------------------

add_executable(polydoc
    polydoc/src/polydoc.cpp
)

target_include_directories(polydoc PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(polydoc
    frontend_ploy
    polyglot_common
)

# -- lsp_lib (Qt-free LSP client core, shared by polyui + polyls + tests) -----
# Introduced by demand 2026-04-28-19.  The library defines the JSON-RPC
# framing, the LSP message types, the abstract transport interface, the
# loopback transport (for unit tests) and the session/capability registry.
# Anything Qt-specific (log panel, stdio-via-QProcess transport) lives in
# the polyui target instead.

add_library(lsp_lib STATIC
    ui/common/lsp/lsp_message.cpp
    ui/common/lsp/lsp_capability_registry.cpp
    ui/common/lsp/lsp_client.cpp
    ui/common/lsp/lsp_session.cpp
)

target_include_directories(lsp_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(lsp_lib PUBLIC nlohmann_json::nlohmann_json)

# -- polyls_core (server-side library, shared by polyls + tests) --------------

add_library(polyls_core STATIC
    polyls/polyls_core/polyls_server.cpp
    polyls/polyls_core/polyls_features.cpp
    polyls/polyls_core/polyls_navigation.cpp
    polyls/polyls_core/polyls_refactor.cpp
    polyls/polyls_core/polyls_semantic.cpp
    polyls/polyls_core/refactor.cpp
    polyls/polyls_core/symbol_index.cpp
    polyls/grammar/grammar_descriptor.cpp
    ui/common/syntax/tree_sitter_runtime.cpp
    ui/common/editing/format_engine.cpp
)

target_include_directories(polyls_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(polyls_core PUBLIC
    lsp_lib
    frontend_ploy
)

# -- polyls (PolyglotCompiler self-hosted Language Server) --------------------

add_executable(polyls
    polyls/polyls.cpp
)

target_link_libraries(polyls
    polyls_core
)

# -- polybench (benchmark suite) -----------------------------------------------

add_executable(polybench
    polybench/src/benchmark_suite.cpp
)

target_link_libraries(polybench
    frontend_python
    frontend_cpp
    frontend_rust
    frontend_ploy
    frontend_java
    frontend_dotnet
    frontend_javascript
    frontend_ruby
    frontend_go
    backend_x86_64
    backend_arm64
    backend_wasm
    runtime
    nlohmann_json::nlohmann_json
    polyglot_tools_settings
)

# -- topo_lib (topology analysis library) --------------------------------------

add_library(topo_lib
    polytopo/src/topology_graph.cpp
    polytopo/src/topology_analyzer.cpp
    polytopo/src/topology_validator.cpp
    polytopo/src/topology_printer.cpp
    polytopo/src/topology_codegen.cpp
)

target_include_directories(topo_lib PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(topo_lib PUBLIC
    polyglot_common
    frontend_ploy
)

target_compile_definitions(topo_lib PRIVATE TOPO_LIBRARY)

# -- polytopo (topology visualization CLI) -------------------------------------

add_executable(polytopo
    polytopo/src/polytopo.cpp
)

target_link_libraries(polytopo
    topo_lib
    polyglot_common
    frontend_ploy
    polyglot_tools_settings
)

# -- polyui (Qt-based desktop IDE) ---------------------------------------------

# Try Qt6 first, fall back to Qt5.  polyui is only built when either is found.
# Prefer the standalone Qt installation over any bundled (e.g. Anaconda) copies.
# The user may pass -DQT_ROOT=<path> to override.
if(NOT DEFINED QT_ROOT)
    if(WIN32 AND EXISTS "D:/Qt")
        set(QT_ROOT "D:/Qt")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/qt")
        set(QT_ROOT "${CMAKE_SOURCE_DIR}/deps/qt")
    endif()
endif()
if(QT_ROOT)
    if(WIN32)
        file(GLOB _qt6_candidates "${QT_ROOT}/*/msvc*_64/lib/cmake/Qt6")
        if(_qt6_candidates)
            list(SORT _qt6_candidates)
            list(GET _qt6_candidates -1 _qt6_best)
            get_filename_component(_qt6_prefix "${_qt6_best}/../../.." ABSOLUTE)
            list(PREPEND CMAKE_PREFIX_PATH "${_qt6_prefix}")
            message(STATUS "Qt hint: ${_qt6_prefix}")
        endif()
        file(GLOB _qt6_mingw "${QT_ROOT}/*/mingw_64/lib/cmake/Qt6")
        if(_qt6_mingw AND NOT _qt6_candidates)
            list(SORT _qt6_mingw)
            list(GET _qt6_mingw -1 _qt6_mg_best)
            get_filename_component(_qt6_mg_prefix "${_qt6_mg_best}/../../.." ABSOLUTE)
            list(PREPEND CMAKE_PREFIX_PATH "${_qt6_mg_prefix}")
            message(STATUS "Qt hint (MinGW): ${_qt6_mg_prefix}")
        endif()
    elseif(APPLE)
        file(GLOB _qt6_mac "${QT_ROOT}/*/macos/lib/cmake/Qt6")
        if(NOT _qt6_mac)
            file(GLOB _qt6_mac "${QT_ROOT}/*/clang_64/lib/cmake/Qt6")
        endif()
        if(_qt6_mac)
            list(SORT _qt6_mac)
            list(GET _qt6_mac -1 _qt6_mac_best)
            get_filename_component(_qt6_mac_prefix "${_qt6_mac_best}/../../.." ABSOLUTE)
            list(PREPEND CMAKE_PREFIX_PATH "${_qt6_mac_prefix}")
            message(STATUS "Qt hint (macOS): ${_qt6_mac_prefix}")
        endif()
    else()
        file(GLOB _qt6_linux "${QT_ROOT}/*/gcc_64/lib/cmake/Qt6")
        if(_qt6_linux)
            list(SORT _qt6_linux)
            list(GET _qt6_linux -1 _qt6_linux_best)
            get_filename_component(_qt6_linux_prefix "${_qt6_linux_best}/../../.." ABSOLUTE)
            list(PREPEND CMAKE_PREFIX_PATH "${_qt6_linux_prefix}")
            message(STATUS "Qt hint (Linux): ${_qt6_linux_prefix}")
        endif()
    endif()
endif()

find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 QUIET COMPONENTS Widgets)
endif()

if(Qt6_FOUND OR Qt5_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)

    # Platform-specific entry point
    if(WIN32)
        set(POLYUI_PLATFORM_MAIN ui/windows/main.cpp)
        set(POLYUI_PLATFORM_RESOURCES ui/windows/polyui.rc)
    elseif(APPLE)
        set(POLYUI_PLATFORM_MAIN ui/macos/main.cpp)
    else()
        set(POLYUI_PLATFORM_MAIN ui/linux/main.cpp)
    endif()

    # Common (cross-platform) sources
    set(POLYUI_COMMON_SOURCES
        ui/common/resources/polyui_icons.qrc
        ui/common/resources/settings_resources.qrc
        ui/common/src/mainwindow.cpp
        ui/common/src/code_editor.cpp
        ui/common/src/syntax_highlighter.cpp
        ui/common/syntax/semantic_tokens_client.cpp
        ui/common/editor/editor_group.cpp
        ui/common/editor/editor_grid.cpp
        ui/common/quickopen/quick_open_ranker.cpp
        ui/common/search/global_search_engine.cpp
        ui/common/outline/outline_model.cpp
        ui/common/editing/multi_cursor.cpp
        ui/common/editing/folding_model.cpp
        ui/common/editing/format_engine.cpp
        ui/common/editing/snippet_engine.cpp
        ui/common/editing/editor_config.cpp
        ui/common/scm/scm_provider.cpp
        ui/common/scm/git_provider.cpp
        ui/common/scm/diff_engine.cpp
        ui/common/scm/merge_resolver.cpp
        ui/common/dap/dap_client.cpp
        ui/common/dap/launch_config.cpp
        ui/common/dap/debug_session.cpp
        ui/common/tasks/task_config.cpp
        ui/common/tasks/problem_matcher.cpp
        ui/common/tasks/task_runner.cpp
        ui/common/runtime/run_debug_picker.cpp
        ui/common/runtime/hot_reload.cpp
        ui/common/src/file_browser.cpp
        ui/common/src/output_panel.cpp
        ui/common/src/compiler_service.cpp
        ui/common/src/terminal_widget.cpp
        ui/common/src/settings_dialog.cpp
        ui/common/src/settings_page.cpp
        ui/common/src/settings_service.cpp
        ui/common/src/keybinding_service.cpp
        ui/common/src/command_palette.cpp
        ui/common/src/git_panel.cpp
        ui/common/src/build_panel.cpp
        ui/common/src/debug_panel.cpp
        ui/common/src/theme_manager.cpp
        ui/common/src/theme_service.cpp
        ui/common/src/theme_manager_view.cpp
        ui/common/src/action_manager.cpp
        ui/common/src/panel_manager.cpp
        ui/common/src/topology_panel.cpp
        ui/common/src/markdown_viewer.cpp
        ui/common/src/polyui_cli.cpp
        ui/common/src/profile_session.cpp
        ui/common/src/profiler_panel.cpp
        ui/common/src/call_analyzer_panel.cpp
        ui/common/src/data_models/flame_node.cpp
        ui/common/src/data_models/call_graph_model.cpp
        ui/common/src/data_models/timeline_model.cpp
        ui/common/lsp/lsp_log_panel.cpp
        ui/common/src/lsp_bridge.cpp
        ui/common/src/code_assist.cpp
        ui/common/src/completion_ranker.cpp
        ui/common/src/problems_aggregator.cpp
        ui/common/src/problems_panel.cpp
        ui/common/src/workspace_scanner.cpp
        polyc/src/foreign_signature_extractor.cpp
    )

    # Headers with Q_OBJECT must be listed for AUTOMOC
    set(POLYUI_COMMON_HEADERS
        ui/common/include/mainwindow.h
        ui/common/include/code_editor.h
        ui/common/include/syntax_highlighter.h
        ui/common/include/file_browser.h
        ui/common/include/output_panel.h
        ui/common/include/terminal_widget.h
        ui/common/include/settings_dialog.h
        ui/common/include/settings_page.h
        ui/common/include/settings_service.h
        ui/common/include/keybinding_service.h
        ui/common/include/command_palette.h
        ui/common/include/git_panel.h
        ui/common/include/build_panel.h
        ui/common/include/debug_panel.h
        ui/common/include/action_manager.h
        ui/common/include/panel_manager.h
        ui/common/include/topology_panel.h
        ui/common/include/markdown_viewer.h
        ui/common/include/theme_service.h
        ui/common/include/theme_manager_view.h
        ui/common/include/profile_session.h
        ui/common/include/profiler_panel.h
        ui/common/include/call_analyzer_panel.h
        ui/common/include/data_models/flame_node.h
        ui/common/include/data_models/call_graph_model.h
        ui/common/include/data_models/timeline_model.h
        ui/common/lsp/lsp_log_panel.h
        ui/common/include/lsp_bridge.h
        ui/common/include/code_assist.h
        ui/common/include/problems_aggregator.h
        ui/common/include/problems_panel.h
        ui/common/include/workspace_scanner.h
    )

    add_executable(polyui
        ${POLYUI_PLATFORM_MAIN}
        ${POLYUI_PLATFORM_RESOURCES}
        ${POLYUI_COMMON_SOURCES}
        ${POLYUI_COMMON_HEADERS}
    )

    target_include_directories(polyui PRIVATE ${CMAKE_SOURCE_DIR})

    if(Qt6_FOUND)
        set(QT_LIB Qt6::Widgets)
        message(STATUS "Qt6 found — building polyui (IDE) [platform main: ${POLYUI_PLATFORM_MAIN}]")
    else()
        set(QT_LIB Qt5::Widgets)
    endif()

    target_link_libraries(polyui
        ${QT_LIB}
        frontend_python
        frontend_cpp
        frontend_rust
        frontend_ploy
        frontend_java
        frontend_dotnet
        frontend_javascript
        frontend_ruby
        frontend_go
        backend_x86_64
        backend_arm64
        backend_wasm
        runtime
        topo_lib
        polyglot_tools_settings
        lsp_lib
        polyls_core
        nlohmann_json::nlohmann_json
    )

    # Windows: mark as GUI app and deploy Qt DLLs
    if(WIN32)
        set_target_properties(polyui PROPERTIES WIN32_EXECUTABLE TRUE)

        if(Qt6_FOUND)
            get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
        else()
            get_target_property(_qt_qmake Qt5::qmake IMPORTED_LOCATION)
        endif()
        get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)

        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" NO_DEFAULT_PATH)
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET polyui POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-compiler-runtime
                    "$<TARGET_FILE:polyui>"
                COMMENT "Running windeployqt to deploy Qt runtime DLLs..."
                VERBATIM
            )
            message(STATUS "windeployqt found at ${WINDEPLOYQT_EXECUTABLE} — DLLs will be auto-deployed")
        else()
            message(WARNING "windeployqt not found — Qt DLLs must be manually copied next to polyui.exe")
        endif()
    endif()

    # macOS: build as a proper .app bundle
    if(APPLE)
        set_target_properties(polyui PROPERTIES
            MACOSX_BUNDLE TRUE
        )
        set(_POLYUI_DYLIB_TARGETS
            frontend_python frontend_cpp frontend_rust frontend_ploy
            frontend_java frontend_dotnet frontend_common
            frontend_javascript frontend_ruby frontend_go
            backend_x86_64 backend_arm64 backend_wasm
            middle_ir runtime polyglot_common linker_lib topo_lib
        )
        foreach(_tgt IN LISTS _POLYUI_DYLIB_TARGETS)
            if(TARGET ${_tgt})
                add_custom_command(TARGET polyui POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        "$<TARGET_BUNDLE_DIR:polyui>/Contents/Frameworks"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:${_tgt}>"
                        "$<TARGET_BUNDLE_DIR:polyui>/Contents/Frameworks/$<TARGET_FILE_NAME:${_tgt}>"
                    COMMENT "Copying ${_tgt} into polyui.app/Contents/Frameworks/..."
                    VERBATIM
                )
            endif()
        endforeach()

        # Fix install_name paths in copied dylibs so macdeployqt does not
        # report "ERROR: Unexpected prefix @executable_path".  The script
        # replaces @rpath references with direct @executable_path/../Frameworks/
        # paths and strips build-tree-only RPATHs from the bundle copies.
        add_custom_command(TARGET polyui POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -D "BUNDLE_DIR=$<TARGET_BUNDLE_DIR:polyui>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/fixup_macos_bundle.cmake"
            COMMENT "Fixing up dylib install names in polyui.app bundle..."
            VERBATIM
        )

        if(Qt6_FOUND)
            get_target_property(_qt_qmake_mac Qt6::qmake IMPORTED_LOCATION)
        else()
            get_target_property(_qt_qmake_mac Qt5::qmake IMPORTED_LOCATION)
        endif()
        get_filename_component(_qt_bin_dir_mac "${_qt_qmake_mac}" DIRECTORY)

        find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir_mac}" NO_DEFAULT_PATH)
        if(MACDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET polyui POST_BUILD
                COMMAND "${MACDEPLOYQT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:polyui>"
                COMMENT "Running macdeployqt to bundle Qt frameworks..."
                VERBATIM
            )
            message(STATUS "macdeployqt found at ${MACDEPLOYQT_EXECUTABLE}")
        endif()
    endif()

    if(UNIX AND NOT APPLE)
        message(STATUS "Building polyui for Linux — ensure Qt runtime libraries are available on the target system")
    endif()
else()
    message(STATUS "Qt NOT found — skipping polyui (IDE). Install Qt5 or Qt6 to build the IDE.")
endif()
