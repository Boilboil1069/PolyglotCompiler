# ============================================================================
# backends/ — code-generation backends
# x86-64, ARM64, and WebAssembly targets.
# ============================================================================

# -- x86-64 backend ------------------------------------------------------------

add_library(backend_x86_64
    x86_64/src/asm_printer/emit.cpp
    x86_64/src/asm_printer/scheduler.cpp
    x86_64/src/isel/isel.cpp
    x86_64/src/regalloc/graph_coloring.cpp
    x86_64/src/regalloc/linear_scan.cpp
    x86_64/src/optimizations.cpp
    x86_64/src/calling_convention.cpp
)

target_include_directories(backend_x86_64 PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(backend_x86_64 PUBLIC polyglot_common middle_ir)

# -- ARM64 backend -------------------------------------------------------------

add_library(backend_arm64
    arm64/src/asm_printer/emit.cpp
    arm64/src/asm_printer/scheduler.cpp
    arm64/src/isel/isel.cpp
    arm64/src/regalloc/graph_coloring.cpp
    arm64/src/regalloc/linear_scan.cpp
    arm64/src/calling_convention.cpp
)

target_include_directories(backend_arm64 PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(backend_arm64 PUBLIC polyglot_common middle_ir)

# -- WebAssembly backend -------------------------------------------------------

add_library(backend_wasm
    wasm/src/wasm_target.cpp
)

target_include_directories(backend_wasm PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(backend_wasm PUBLIC polyglot_common middle_ir)
