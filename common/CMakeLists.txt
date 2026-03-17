# ============================================================================
# common/ — polyglot_common library
# Core type system, symbol table, plugin manager, and debug info utilities.
# ============================================================================

add_library(polyglot_common
    src/core/type_system.cpp
    src/core/symbol_table.cpp
    src/plugins/plugin_manager.cpp
    ${CMAKE_SOURCE_DIR}/backends/common/src/debug_info.cpp
    ${CMAKE_SOURCE_DIR}/backends/common/src/debug_emitter.cpp
    ${CMAKE_SOURCE_DIR}/backends/common/src/dwarf_builder.cpp
    ${CMAKE_SOURCE_DIR}/backends/common/src/object_file.cpp
    src/debug/dwarf5.cpp
)

target_include_directories(polyglot_common PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(polyglot_common PUBLIC
    fmt::fmt-header-only
    nlohmann_json::nlohmann_json
)
