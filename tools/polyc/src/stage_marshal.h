#pragma once
// ============================================================================
// stage_marshal.h — Stage 3: Marshal Plan Generation
//
// Inputs:  DriverSettings + SemanticResult
// Outputs: MarshalResult (cross-language parameter conversion plan)
//
// Responsibilities:
//   - For each LinkEntry, build a MarshalCallPlan describing the parameter
//     count and ABI requirements for the bridge stub generator.
//   - For non-.ploy sources or empty link entries, returns trivially.
// ============================================================================

#include "tools/polyc/include/driver_stages.h"

namespace polyglot::tools {

/// Execute Stage 3: Cross-language marshal plan generation.
MarshalResult RunMarshalStage(const DriverSettings &settings,
                               const SemanticResult &semantic);

}  // namespace polyglot::tools
