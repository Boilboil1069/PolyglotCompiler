// topology_codegen.h — Generates .ploy source code from a TopologyGraph.
//
// Provides bidirectional interop: given a topology graph (either parsed
// from .ploy or loaded from JSON), regenerate valid .ploy source text.
// Shared between the polyui IDE (Generate .ploy button) and the polytopo
// CLI (generate subcommand).

#pragma once

#include <string>

#include "tools/polytopo/include/topology_graph.h"

namespace polyglot::tools::topo {

// ============================================================================
// GeneratePloySrc — graph-to-source code generator
// ============================================================================

// Convert a TopologyGraph into valid .ploy source code.
//
// Generation rules:
//   - kFunction   node  ->  FUNC name(inputs) -> output_type { body }
//   - kConstructor node ->  NEW(language, class, args) expression
//   - kPipeline   node  ->  PIPELINE name { ... }
//   - kExternalCall node -> CALL(language, function, args) expression
//   - kMapFunc    node  ->  MAP_FUNC name(inputs) -> output_type { body }
//   - kMethod     node  ->  METHOD(language, object, method, args) expression
//   - Each edge   ->  LINK or CALL statement connecting source to target
//   - Distinct language pairs produce IMPORT directives
//   - Distinct type conversions produce MAP_TYPE directives
//
// The generated code is guaranteed to be parseable by PloyParser.
std::string GeneratePloySrc(const TopologyGraph &graph);

// ============================================================================
// ParseJsonToGraph — reconstruct a TopologyGraph from JSON
// ============================================================================

// Parse a JSON string (as produced by TopologyPrinter::PrintJson) back
// into a TopologyGraph.  Returns true on success.
bool ParseJsonToGraph(const std::string &json_str, TopologyGraph &out_graph);

} // namespace polyglot::tools::topo
