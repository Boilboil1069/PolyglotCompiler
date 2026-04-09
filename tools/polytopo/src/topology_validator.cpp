// topology_validator.cpp — Validates a TopologyGraph for correctness.

#include "tools/polytopo/include/topology_validator.h"

#include <algorithm>
#include <unordered_set>

namespace polyglot::tools::topo {

// ============================================================================
// Construction
// ============================================================================

TopologyValidator::TopologyValidator(const ValidationOptions &opts)
    : opts_(opts) {}

// ============================================================================
// Public API
// ============================================================================

bool TopologyValidator::Validate(const TopologyGraph &graph) {
    diagnostics_.clear();

    ValidateEdgeTypes(graph);
    ValidateUnconnectedPorts(graph);
    ValidateParameterCounts(graph);
    ValidateCycles(graph);
    ValidateLanguageCompatibility(graph);

    return ErrorCount() == 0;
}

size_t TopologyValidator::ErrorCount() const {
    return static_cast<size_t>(std::count_if(
        diagnostics_.begin(), diagnostics_.end(),
        [](const ValidationDiagnostic &d) {
            return d.severity == ValidationDiagnostic::Severity::kError;
        }));
}

size_t TopologyValidator::WarningCount() const {
    return static_cast<size_t>(std::count_if(
        diagnostics_.begin(), diagnostics_.end(),
        [](const ValidationDiagnostic &d) {
            return d.severity == ValidationDiagnostic::Severity::kWarning;
        }));
}

size_t TopologyValidator::InfoCount() const {
    return static_cast<size_t>(std::count_if(
        diagnostics_.begin(), diagnostics_.end(),
        [](const ValidationDiagnostic &d) {
            return d.severity == ValidationDiagnostic::Severity::kInfo;
        }));
}

// ============================================================================
// Edge type validation
// ============================================================================

void TopologyValidator::ValidateEdgeTypes(const TopologyGraph &graph) {
    for (const auto &edge : graph.Edges()) {
        const auto *src_node = graph.GetNode(edge.source_node_id);
        const auto *tgt_node = graph.GetNode(edge.target_node_id);
        if (!src_node || !tgt_node) {
            AddDiagnostic(ValidationDiagnostic::Severity::kError,
                          "Edge references non-existent node",
                          edge.loc, 0, edge.id);
            continue;
        }

        // Find the source output port
        const Port *src_port = nullptr;
        for (const auto &p : src_node->outputs) {
            if (p.id == edge.source_port_id) {
                src_port = &p;
                break;
            }
        }

        // Find the target input port
        const Port *tgt_port = nullptr;
        for (const auto &p : tgt_node->inputs) {
            if (p.id == edge.target_port_id) {
                tgt_port = &p;
                break;
            }
        }

        if (!src_port) {
            AddDiagnostic(ValidationDiagnostic::Severity::kError,
                          "Edge source port not found on node '" + src_node->name + "'",
                          edge.loc, src_node->id, edge.id);
            continue;
        }
        if (!tgt_port) {
            AddDiagnostic(ValidationDiagnostic::Severity::kError,
                          "Edge target port not found on node '" + tgt_node->name + "'",
                          edge.loc, tgt_node->id, edge.id);
            continue;
        }

        // Check type compatibility
        auto status = CheckTypeCompatibility(
            src_port->type, tgt_port->type,
            src_node->language, tgt_node->language);

        // Update the edge status (const cast is acceptable here since we only
        // modify the status field for reporting; the graph structure is unchanged)
        const_cast<TopologyEdge &>(edge).status = status;

        switch (status) {
        case TopologyEdge::Status::kValid:
            // No diagnostic needed
            break;
        case TopologyEdge::Status::kImplicitConvert:
            AddDiagnostic(ValidationDiagnostic::Severity::kInfo,
                          "Implicit type conversion: " + src_port->type.name +
                              " (" + src_node->language + ") -> " +
                              tgt_port->type.name + " (" + tgt_node->language + ")",
                          edge.loc, 0, edge.id);
            break;
        case TopologyEdge::Status::kExplicitConvert:
            AddDiagnostic(ValidationDiagnostic::Severity::kWarning,
                          "Explicit type conversion required: " +
                              src_port->type.name + " (" + src_node->language +
                              ") -> " + tgt_port->type.name + " (" +
                              tgt_node->language + ")",
                          edge.loc, 0, edge.id);
            break;
        case TopologyEdge::Status::kIncompatible:
            AddDiagnostic(ValidationDiagnostic::Severity::kError,
                          "Incompatible types: " + src_port->type.name +
                              " (" + src_node->language + ") cannot convert to " +
                              tgt_port->type.name + " (" + tgt_node->language + ")",
                          edge.loc, 0, edge.id);
            break;
        case TopologyEdge::Status::kUnknown: {
            auto severity = opts_.strict_any
                                ? ValidationDiagnostic::Severity::kError
                                : ValidationDiagnostic::Severity::kWarning;
            AddDiagnostic(severity,
                          "Type unknown on edge from '" + src_node->name + ":" +
                              src_port->name + "' to '" + tgt_node->name + ":" +
                              tgt_port->name + "'",
                          edge.loc, 0, edge.id);
            break;
        }
        }
    }
}

// ============================================================================
// Unconnected port validation
// ============================================================================

void TopologyValidator::ValidateUnconnectedPorts(const TopologyGraph &graph) {
    // Collect all connected port ids
    std::unordered_set<uint64_t> connected_inputs;
    std::unordered_set<uint64_t> connected_outputs;
    for (const auto &edge : graph.Edges()) {
        connected_outputs.insert(edge.source_port_id);
        connected_inputs.insert(edge.target_port_id);
    }

    for (const auto &node : graph.Nodes()) {
        if (opts_.warn_unconnected_inputs) {
            for (const auto &port : node.inputs) {
                if (connected_inputs.find(port.id) == connected_inputs.end()) {
                    AddDiagnostic(ValidationDiagnostic::Severity::kWarning,
                                  "Unconnected input port '" + port.name +
                                      "' on node '" + node.name + "'",
                                  node.loc, node.id, 0, port.id);
                }
            }
        }
        if (opts_.warn_unconnected_outputs) {
            for (const auto &port : node.outputs) {
                if (connected_outputs.find(port.id) == connected_outputs.end()) {
                    AddDiagnostic(ValidationDiagnostic::Severity::kWarning,
                                  "Unconnected output port '" + port.name +
                                      "' on node '" + node.name + "'",
                                  node.loc, node.id, 0, port.id);
                }
            }
        }
    }
}

// ============================================================================
// Parameter count validation
// ============================================================================

void TopologyValidator::ValidateParameterCounts(const TopologyGraph &graph) {
    for (const auto &node : graph.Nodes()) {
        // Count incoming edges per input port
        std::unordered_map<uint64_t, size_t> incoming_count;
        for (const auto &port : node.inputs) {
            incoming_count[port.id] = 0;
        }
        for (const auto &edge : graph.Edges()) {
            if (edge.target_node_id == node.id) {
                incoming_count[edge.target_port_id]++;
            }
        }

        // Check for ports with multiple incoming edges (fan-in)
        for (const auto &[port_id, count] : incoming_count) {
            if (count > 1) {
                // Find port name for better diagnostic
                std::string port_name = "unknown";
                for (const auto &p : node.inputs) {
                    if (p.id == port_id) {
                        port_name = p.name;
                        break;
                    }
                }
                AddDiagnostic(ValidationDiagnostic::Severity::kWarning,
                              "Input port '" + port_name + "' on node '" +
                                  node.name + "' has " + std::to_string(count) +
                                  " incoming connections (possible ambiguity)",
                              node.loc, node.id, 0, port_id);
            }
        }
    }
}

// ============================================================================
// Cycle detection
// ============================================================================

void TopologyValidator::ValidateCycles(const TopologyGraph &graph) {
    if (opts_.allow_cycles) return;

    auto cycle = graph.DetectCycles();
    if (!cycle.empty()) {
        std::string cycle_str;
        for (size_t i = 0; i < cycle.size(); ++i) {
            const auto *node = graph.GetNode(cycle[i]);
            if (node) {
                if (i > 0) cycle_str += " -> ";
                cycle_str += node->name;
            }
        }
        AddDiagnostic(ValidationDiagnostic::Severity::kError,
                       "Cycle detected in topology graph: " + cycle_str);
    }
}

// ============================================================================
// Language compatibility validation
// ============================================================================

void TopologyValidator::ValidateLanguageCompatibility(const TopologyGraph &graph) {
    // Check for edges between nodes of the same language that go through
    // cross-language marshalling (likely a configuration error)
    for (const auto &edge : graph.Edges()) {
        const auto *src = graph.GetNode(edge.source_node_id);
        const auto *tgt = graph.GetNode(edge.target_node_id);
        if (!src || !tgt) continue;

        // Same-language edges within ploy are normal
        if (src->language == tgt->language && src->language == "ploy") continue;

        // Cross-language edges require marshalling — just informational
        if (src->language != tgt->language) {
            AddDiagnostic(ValidationDiagnostic::Severity::kInfo,
                          "Cross-language edge: " + src->language +
                              " -> " + tgt->language +
                              " (marshalling required)",
                          edge.loc, 0, edge.id);
        }
    }
}

// ============================================================================
// Type compatibility checking
// ============================================================================

TopologyEdge::Status TopologyValidator::CheckTypeCompatibility(
    const core::Type &source, const core::Type &target,
    const std::string &source_lang, const std::string &target_lang) const {

    // If either type is Any, we cannot verify
    if (source.kind == core::TypeKind::kAny || target.kind == core::TypeKind::kAny) {
        return TopologyEdge::Status::kUnknown;
    }

    // If either type is Invalid, it is an error
    if (source.kind == core::TypeKind::kInvalid || target.kind == core::TypeKind::kInvalid) {
        return TopologyEdge::Status::kIncompatible;
    }

    // Same kind and same name: directly compatible
    if (source.kind == target.kind) {
        if (source.name == target.name || source.name.empty() || target.name.empty()) {
            return TopologyEdge::Status::kValid;
        }
    }

    // Numeric promotions (int -> float, i32 -> i64, etc.)
    if (source.kind == core::TypeKind::kInt && target.kind == core::TypeKind::kFloat) {
        return TopologyEdge::Status::kImplicitConvert;
    }
    if (source.kind == core::TypeKind::kFloat && target.kind == core::TypeKind::kInt) {
        return TopologyEdge::Status::kExplicitConvert;
    }

    // Bool to/from int
    if ((source.kind == core::TypeKind::kBool && target.kind == core::TypeKind::kInt) ||
        (source.kind == core::TypeKind::kInt && target.kind == core::TypeKind::kBool)) {
        return TopologyEdge::Status::kImplicitConvert;
    }

    // String conversions typically need explicit conversion
    if (source.kind == core::TypeKind::kString || target.kind == core::TypeKind::kString) {
        if (source.kind != target.kind) {
            return TopologyEdge::Status::kExplicitConvert;
        }
    }

    // Pointer types are implicitly convertible across languages
    if (source.kind == core::TypeKind::kPointer && target.kind == core::TypeKind::kPointer) {
        return TopologyEdge::Status::kValid;
    }

    // Class types: check name match
    if (source.kind == core::TypeKind::kClass && target.kind == core::TypeKind::kClass) {
        if (source.name == target.name) {
            return TopologyEdge::Status::kValid;
        }
        return TopologyEdge::Status::kExplicitConvert;
    }

    // Tuple/array/struct conversions require explicit mapping
    if (source.kind != target.kind) {
        // Check for common cross-language conversions
        bool source_is_container = source.kind == core::TypeKind::kArray ||
                                   source.kind == core::TypeKind::kTuple ||
                                   source.kind == core::TypeKind::kStruct;
        bool target_is_container = target.kind == core::TypeKind::kArray ||
                                   target.kind == core::TypeKind::kTuple ||
                                   target.kind == core::TypeKind::kStruct;
        if (source_is_container && target_is_container) {
            return TopologyEdge::Status::kExplicitConvert;
        }
        return TopologyEdge::Status::kIncompatible;
    }

    return TopologyEdge::Status::kValid;
}

// ============================================================================
// Diagnostic helpers
// ============================================================================

void TopologyValidator::AddDiagnostic(
    ValidationDiagnostic::Severity severity,
    const std::string &message,
    const core::SourceLoc &loc,
    uint64_t node_id,
    uint64_t edge_id,
    uint64_t port_id) {
    ValidationDiagnostic diag;
    diag.severity = severity;
    diag.message = message;
    diag.loc = loc;
    diag.node_id = node_id;
    diag.edge_id = edge_id;
    diag.port_id = port_id;
    diagnostics_.push_back(std::move(diag));
}

} // namespace polyglot::tools::topo
