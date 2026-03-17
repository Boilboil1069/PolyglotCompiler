# ============================================================================
# middle/ — middle_ir library
# Intermediate representation, optimization passes, PGO, and LTO.
# ============================================================================

add_library(middle_ir
    src/ir/builder.cpp
    src/ir/ir_context.cpp
    src/ir/cfg.cpp
    src/ir/analysis.cpp
    src/ir/data_layout.cpp
    src/ir/ssa.cpp
    src/ir/verifier.cpp
    src/ir/printer.cpp
    src/ir/parser.cpp
    src/ir/template_instantiator.cpp
    src/ir/passes/opt.cpp
    src/passes/pass_manager.cpp
    src/passes/optimizations/constant_fold.cpp
    src/passes/optimizations/dead_code_elim.cpp
    src/passes/optimizations/common_subexpr.cpp
    src/passes/optimizations/inlining.cpp
    src/passes/optimizations/devirtualization.cpp
    src/passes/optimizations/loop_optimization.cpp
    src/passes/optimizations/gvn.cpp
    src/passes/optimizations/advanced_optimizations.cpp
    src/pgo/profile_data.cpp
    src/lto/link_time_optimizer.cpp
)

target_include_directories(middle_ir PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(middle_ir PUBLIC polyglot_common)
