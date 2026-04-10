/**
 * @file     topology_validator.h
 * @brief    Validates a TopologyGraph for correctness
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>
#include <vector>

#include "tools/polytopo/include/topology_graph.h"

namespace polyglot::tools::topo {

// ============================================================================
// ValidationOptions
// ============================================================================

/** @brief ValidationOptions data structure. */
struct ValidationOptions {
    // Treat Any-typed connections as warnings (default) or errors (strict)
    bool strict_any{false};

    // Allow cycles (some pipeline patterns intentionally loop)
    bool allow_cycles{false};

    // Warn on unconnected input ports
    bool warn_unconnected_inputs{true};

    // Warn on unconnected output ports
    bool warn_unconnected_outputs{false};

    // Maximum allowed graph depth (0 = no limit)
    size_t max_depth{0};
};

// ============================================================================
// TopologyValidator
// ============================================================================

/** @brief TopologyValidator class. */
class TopologyValidator {
  public:
    explicit TopologyValidator(const ValidationOptions &opts = {});
    ~TopologyValidator() = default;

    // Run all validation checks on the given graph.
    // Returns true if no errors were found (warnings are acceptable).
    bool Validate(const TopologyGraph &graph);

    // Access diagnostics produced by the last Validate() call
    const std::vector<ValidationDiagnostic> &Diagnostics() const { return diagnostics_; }

    // Convenience: count errors / warnings / info
    size_t ErrorCount() const;
    size_t WarningCount() const;
    size_t InfoCount() const;

  private:
    // Individual validation passes
    void ValidateEdgeTypes(const TopologyGraph &graph);
    void ValidateUnconnectedPorts(const TopologyGraph &graph);
    void ValidateParameterCounts(const TopologyGraph &graph);
    void ValidateCycles(const TopologyGraph &graph);
    void ValidateLanguageCompatibility(const TopologyGraph &graph);

    // Type compatibility check between two types across languages
    TopologyEdge::Status CheckTypeCompatibility(const core::Type &source,
                                                const core::Type &target,
                                                const std::string &source_lang,
                                                const std::string &target_lang) const;

    void AddDiagnostic(ValidationDiagnostic::Severity severity,
                       const std::string &message,
                       const core::SourceLoc &loc = {},
                       uint64_t node_id = 0,
                       uint64_t edge_id = 0,
                       uint64_t port_id = 0);

    ValidationOptions opts_;
    std::vector<ValidationDiagnostic> diagnostics_;
};

} // namespace polyglot::tools::topo
