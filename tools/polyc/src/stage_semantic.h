#pragma once
// ============================================================================
// stage_semantic.h — Stage 2: Semantic Analysis
//
// Inputs:  DriverSettings + FrontendResult
// Outputs: SemanticResult (validated AST, symbol table, link entries)
//
// Responsibilities:
//   - Run PloySema::Analyze() on the AST
//   - Export symbol table, known signatures, link entries, type mappings
//   - Only valid for .ploy; non-.ploy IR is passed through transparently
// ============================================================================

#include "tools/polyc/include/driver_stages.h"

namespace polyglot::tools {

/// Execute Stage 2: Semantic analysis (only meaningful for .ploy sources).
/// For non-.ploy languages the FrontendResult already contains a complete IR;
/// in that case SemanticResult::success is set to true with empty metadata.
SemanticResult RunSemanticStage(const DriverSettings &settings,
                                const FrontendResult &frontend);

}  // namespace polyglot::tools
