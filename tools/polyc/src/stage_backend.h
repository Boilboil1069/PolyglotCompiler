/**
 * @file     stage_backend.h
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once
// ============================================================================
// stage_backend.h — Stage 5: IR optimisation + code generation
//
// Inputs:  DriverSettings + FrontendResult + SemanticResult + BridgeResult
// Outputs: BackendResult (ObjSection/ObjSymbol lists, assembly text, IR text)
//
// Responsibilities:
//   - For .ploy: IR lowering via PloyLowering, inject bridge stubs
//   - SSA conversion + IR verification
//   - PassManager(opt_level).Build() + RunOnModule() — NOT hard-wired passes
//   - Target-specific code generation (x86_64 / arm64 / wasm)
//   - Map MCResult → ObjSection/ObjSymbol/ObjReloc
// ============================================================================

#include "tools/polyc/include/driver_stages.h"

namespace polyglot::tools {

/// Execute Stage 5: IR lowering + optimisation + code generation.
BackendResult RunBackendStage(const DriverSettings &settings,
                               const FrontendResult &frontend,
                               const SemanticResult &semantic,
                               const BridgeResult   &bridge);

}  // namespace polyglot::tools
