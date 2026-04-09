#pragma once
// ============================================================================
// stage_bridge.h — Stage 4: Bridge Stub Generation
//
// Inputs:  DriverSettings + MarshalResult + SemanticResult + aux_dir/stem
// Outputs: BridgeResult (resolved glue stubs + path to descriptor file)
//
// Responsibilities:
//   - Build PolyglotLinker with all CallDescriptors + LinkEntries + Symbols
//   - Call ResolveLinks() to generate glue stubs
//   - Write serialized cross-language descriptor file to aux dir
//     (text format: LINK/CALL/SYMBOL lines, readable by polyld --ploy-desc)
// ============================================================================

#include <string>

#include "tools/polyc/include/driver_stages.h"

namespace polyglot::tools {

/// Execute Stage 4: Cross-language bridge stub generation.
/// @param aux_dir  Path to the aux/ directory (may be empty — skip file write)
/// @param stem     Source file stem used to name the descriptor file
BridgeResult RunBridgeStage(const DriverSettings &settings,
                             const MarshalResult &marshal,
                             const SemanticResult &semantic,
                             const std::string &aux_dir,
                             const std::string &stem);

}  // namespace polyglot::tools
