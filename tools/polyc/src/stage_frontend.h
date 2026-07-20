/**
 * @file     stage_frontend.h
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once
// ============================================================================
// stage_frontend.h — Stage 1: Preprocessing + Lexing + Parsing
//
// Inputs:  DriverSettings (source text, language, include paths, …)
// Outputs: FrontendResult (AST for .poly; direct IR ctx for other languages)
//
// Responsibilities:
//   - Run preprocessor for languages that need it (cpp, …)
//   - Run package-index phase for .poly (shells out to pip/cargo/etc.)
//   - Lex + parse .poly source → AST
//   - Dispatch non-.poly sources through FrontendRegistry → IR directly
// ============================================================================

#include "tools/polyc/include/driver_stages.h"

namespace polyglot::tools {

/// Execute Stage 1: Preprocessing + Lexing + Parsing.
/// Returns a fully populated FrontendResult; call result.success to check.
FrontendResult RunFrontendStage(const DriverSettings &settings);

} // namespace polyglot::tools
