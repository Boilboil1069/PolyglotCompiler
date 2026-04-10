/**
 * @file     topology_printer.h
 * @brief    Renders a TopologyGraph in various text formats
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <ostream>
#include <string>

#include "tools/polytopo/include/topology_graph.h"

namespace polyglot::tools::topo {

// ============================================================================
// PrintOptions
// ============================================================================

/** @brief PrintOptions data structure. */
struct PrintOptions {
    // Show type annotations on ports
    bool show_types{true};

    // Show edge status labels
    bool show_edge_status{true};

    // Show source locations
    bool show_locations{false};

    // Use color (ANSI escape codes) in text output
    bool use_color{true};

    // Compact mode: suppress decorative lines
    bool compact{false};

    // DOT options
    std::string dot_graph_name{"topology"};
    bool dot_horizontal{false};  // LR layout instead of TB
};

// ============================================================================
// TopologyPrinter
// ============================================================================

/** @brief TopologyPrinter class. */
class TopologyPrinter {
  public:
    explicit TopologyPrinter(const PrintOptions &opts = {});
    ~TopologyPrinter() = default;

    // Render the graph as human-readable ASCII art
    void PrintText(const TopologyGraph &graph, std::ostream &out) const;

    // Render the graph in Graphviz DOT format
    void PrintDot(const TopologyGraph &graph, std::ostream &out) const;

    // Render the graph as JSON
    void PrintJson(const TopologyGraph &graph, std::ostream &out) const;

    // Render a compact summary (node count, edge count, issues)
    void PrintSummary(const TopologyGraph &graph, std::ostream &out) const;

    // Render validation diagnostics
    void PrintDiagnostics(const std::vector<ValidationDiagnostic> &diagnostics,
                          std::ostream &out) const;

  private:
    // Helpers for text rendering
    std::string FormatNodeBox(const TopologyNode &node) const;
    std::string FormatPort(const Port &port) const;
    std::string FormatEdgeStatus(TopologyEdge::Status status) const;
    std::string ColorString(const std::string &text, const std::string &color) const;

    // Helpers for DOT rendering
    std::string DotNodeId(uint64_t node_id) const;
    std::string DotPortId(uint64_t port_id) const;
    std::string DotNodeColor(const TopologyNode &node) const;
    std::string DotEdgeColor(TopologyEdge::Status status) const;

    PrintOptions opts_;
};

} // namespace polyglot::tools::topo
