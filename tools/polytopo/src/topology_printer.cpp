/**
 * @file     topology_printer.cpp
 * @brief    Renders a TopologyGraph in various text formats
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/polytopo/include/topology_printer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace polyglot::tools::topo {

// ============================================================================
// Construction
// ============================================================================

TopologyPrinter::TopologyPrinter(const PrintOptions &opts)
    : opts_(opts) {}

// ============================================================================
// ANSI color helpers
// ============================================================================

static const char *kReset = "\033[0m";
static const char *kBold = "\033[1m";
static const char *kRed = "\033[31m";
static const char *kGreen = "\033[32m";
static const char *kYellow = "\033[33m";
static const char *kBlue = "\033[34m";
static const char *kMagenta = "\033[35m";
static const char *kCyan = "\033[36m";
static const char *kGray = "\033[90m";

std::string TopologyPrinter::ColorString(const std::string &text,
                                          const std::string &color) const {
    if (!opts_.use_color) return text;
    return color + text + kReset;
}

// ============================================================================
// Text output
// ============================================================================

void TopologyPrinter::PrintText(const TopologyGraph &graph, std::ostream &out) const {
    // Header
    out << ColorString("╔══════════════════════════════════════════════════╗", kCyan) << "\n";
    out << ColorString("║", kCyan) << ColorString("        POLYGLOT TOPOLOGY GRAPH", kBold)
        << "                  " << ColorString("║", kCyan) << "\n";
    if (!graph.module_name.empty()) {
        std::string mod_line = "  Module: " + graph.module_name;
        mod_line.resize(48, ' ');
        out << ColorString("║", kCyan) << " " << mod_line << ColorString("║", kCyan) << "\n";
    }
    out << ColorString("╚══════════════════════════════════════════════════╝", kCyan) << "\n\n";

    // Summary line
    out << ColorString("Nodes: ", kBold) << graph.NodeCount()
        << "  " << ColorString("Edges: ", kBold) << graph.EdgeCount() << "\n\n";

    // Topological order (or all nodes if cycle)
    auto sorted = graph.TopologicalSort();
    const auto &nodes = sorted.empty() ? graph.Nodes()
                                       : std::vector<TopologyNode>{};
    // Build display list
    std::vector<const TopologyNode *> display_list;
    if (!sorted.empty()) {
        display_list = sorted;
    } else {
        for (const auto &n : graph.Nodes()) {
            display_list.push_back(&n);
        }
    }

    // Print each node
    for (const auto *node : display_list) {
        out << FormatNodeBox(*node) << "\n";

        // Print outgoing edges
        auto out_edges = graph.OutEdges(node->id);
        for (const auto *edge : out_edges) {
            const auto *target = graph.GetNode(edge->target_node_id);
            if (!target) continue;

            // Find port names
            std::string src_port_name = "?";
            for (const auto &p : node->outputs) {
                if (p.id == edge->source_port_id) {
                    src_port_name = p.name;
                    break;
                }
            }
            std::string tgt_port_name = "?";
            for (const auto &p : target->inputs) {
                if (p.id == edge->target_port_id) {
                    tgt_port_name = p.name;
                    break;
                }
            }

            std::string status_str = FormatEdgeStatus(edge->status);
            std::string arrow = "  " + ColorString("└──▶", kGray) + " ";
            out << arrow << ColorString(target->name, kBlue)
                << ":" << tgt_port_name;
            if (opts_.show_edge_status) {
                out << "  " << status_str;
            }
            out << "\n";
        }
    }
    out << "\n";

    // Language distribution
    auto lang_dist = graph.LanguageDistribution();
    if (!lang_dist.empty()) {
        out << ColorString("Language Distribution:", kBold) << "\n";
        for (const auto &[lang, count] : lang_dist) {
            out << "  " << lang << ": " << count << " node(s)\n";
        }
        out << "\n";
    }
}

void TopologyPrinter::PrintSummary(const TopologyGraph &graph, std::ostream &out) const {
    out << "Topology Summary for '" << graph.module_name << "':\n";
    out << "  Nodes:  " << graph.NodeCount() << "\n";
    out << "  Edges:  " << graph.EdgeCount() << "\n";

    auto roots = graph.Roots();
    out << "  Roots:  " << roots.size() << " (";
    for (size_t i = 0; i < roots.size(); ++i) {
        if (i > 0) out << ", ";
        out << roots[i]->name;
    }
    out << ")\n";

    auto leaves = graph.Leaves();
    out << "  Leaves: " << leaves.size() << " (";
    for (size_t i = 0; i < leaves.size(); ++i) {
        if (i > 0) out << ", ";
        out << leaves[i]->name;
    }
    out << ")\n";

    auto cycles = graph.DetectCycles();
    if (cycles.empty()) {
        out << "  Cycles: none (DAG)\n";
    } else {
        out << "  Cycles: DETECTED (" << cycles.size() << " nodes)\n";
    }

    auto edge_dist = graph.EdgeStatusDistribution();
    if (!edge_dist.empty()) {
        out << "  Edge Status:\n";
        auto status_name = [](TopologyEdge::Status s) -> std::string {
            switch (s) {
            case TopologyEdge::Status::kValid: return "Valid";
            case TopologyEdge::Status::kImplicitConvert: return "Implicit Convert";
            case TopologyEdge::Status::kExplicitConvert: return "Explicit Convert";
            case TopologyEdge::Status::kIncompatible: return "Incompatible";
            case TopologyEdge::Status::kUnknown: return "Unknown";
            }
            return "?";
        };
        for (const auto &[status, count] : edge_dist) {
            out << "    " << status_name(status) << ": " << count << "\n";
        }
    }
}

std::string TopologyPrinter::FormatNodeBox(const TopologyNode &node) const {
    std::ostringstream oss;

    // Node kind icon
    std::string kind_icon;
    switch (node.kind) {
    case TopologyNode::Kind::kFunction:     kind_icon = "fn"; break;
    case TopologyNode::Kind::kConstructor:  kind_icon = "ctor"; break;
    case TopologyNode::Kind::kMethod:       kind_icon = "method"; break;
    case TopologyNode::Kind::kPipeline:     kind_icon = "pipe"; break;
    case TopologyNode::Kind::kMapFunc:      kind_icon = "map"; break;
    case TopologyNode::Kind::kExternalCall: kind_icon = "ext"; break;
    }

    // Header line
    oss << ColorString("┌─", kGray);
    std::string lang_tag = node.language.empty() ? "" : "[" + node.language + "]";
    oss << ColorString("[" + kind_icon + "]", kMagenta) << " "
        << ColorString(node.name, kBold + std::string(kGreen))
        << " " << ColorString(lang_tag, kGray);

    if (node.is_linked) {
        oss << " " << ColorString("LINKED", kYellow)
            << ColorString("(" + node.link_source_language + "::" +
                               node.link_source_function + ")",
                           kGray);
    }
    oss << "\n";

    // Input ports
    if (!node.inputs.empty()) {
        oss << ColorString("│", kGray) << " Inputs:\n";
        for (const auto &port : node.inputs) {
            oss << ColorString("│", kGray) << "   "
                << ColorString("◉", kBlue) << " " << port.name;
            if (opts_.show_types) {
                oss << ": " << ColorString(port.type.name.empty() ? "Any" : port.type.name, kCyan);
            }
            oss << "\n";
        }
    }

    // Output ports
    if (!node.outputs.empty()) {
        oss << ColorString("│", kGray) << " Outputs:\n";
        for (const auto &port : node.outputs) {
            oss << ColorString("│", kGray) << "   "
                << ColorString("◎", kGreen) << " " << port.name;
            if (opts_.show_types) {
                oss << ": " << ColorString(port.type.name.empty() ? "Any" : port.type.name, kCyan);
            }
            oss << "\n";
        }
    }

    oss << ColorString("└─", kGray);

    if (opts_.show_locations && (node.loc.line > 0 || !node.loc.file.empty())) {
        oss << " " << ColorString(node.loc.file + ":" + std::to_string(node.loc.line), kGray);
    }

    return oss.str();
}

std::string TopologyPrinter::FormatPort(const Port &port) const {
    std::string dir = port.direction == Port::Direction::kInput ? "IN" : "OUT";
    return dir + " " + port.name + ": " + (port.type.name.empty() ? "Any" : port.type.name);
}

std::string TopologyPrinter::FormatEdgeStatus(TopologyEdge::Status status) const {
    switch (status) {
    case TopologyEdge::Status::kValid:
        return ColorString("[OK]", kGreen);
    case TopologyEdge::Status::kImplicitConvert:
        return ColorString("[IMPLICIT]", kYellow);
    case TopologyEdge::Status::kExplicitConvert:
        return ColorString("[EXPLICIT]", kYellow);
    case TopologyEdge::Status::kIncompatible:
        return ColorString("[ERROR]", kRed);
    case TopologyEdge::Status::kUnknown:
        return ColorString("[UNKNOWN]", kGray);
    }
    return "[?]";
}

// ============================================================================
// DOT output
// ============================================================================

std::string TopologyPrinter::DotNodeId(uint64_t node_id) const {
    return "n" + std::to_string(node_id);
}

std::string TopologyPrinter::DotPortId(uint64_t port_id) const {
    return "p" + std::to_string(port_id);
}

std::string TopologyPrinter::DotNodeColor(const TopologyNode &node) const {
    switch (node.kind) {
    case TopologyNode::Kind::kFunction:     return "#4CAF50";
    case TopologyNode::Kind::kConstructor:  return "#2196F3";
    case TopologyNode::Kind::kMethod:       return "#9C27B0";
    case TopologyNode::Kind::kPipeline:     return "#FF9800";
    case TopologyNode::Kind::kMapFunc:      return "#795548";
    case TopologyNode::Kind::kExternalCall: return "#607D8B";
    }
    return "#9E9E9E";
}

std::string TopologyPrinter::DotEdgeColor(TopologyEdge::Status status) const {
    switch (status) {
    case TopologyEdge::Status::kValid:           return "#4CAF50";
    case TopologyEdge::Status::kImplicitConvert: return "#FFC107";
    case TopologyEdge::Status::kExplicitConvert: return "#FF9800";
    case TopologyEdge::Status::kIncompatible:    return "#F44336";
    case TopologyEdge::Status::kUnknown:         return "#9E9E9E";
    }
    return "#000000";
}

void TopologyPrinter::PrintDot(const TopologyGraph &graph, std::ostream &out) const {
    out << "digraph " << opts_.dot_graph_name << " {\n";
    out << "  rankdir=" << (opts_.dot_horizontal ? "LR" : "TB") << ";\n";
    out << "  node [shape=record, fontname=\"Consolas\", fontsize=10];\n";
    out << "  edge [fontname=\"Consolas\", fontsize=8];\n";
    out << "  bgcolor=\"#FAFAFA\";\n\n";

    // Nodes
    for (const auto &node : graph.Nodes()) {
        std::string nid = DotNodeId(node.id);
        std::string color = DotNodeColor(node);

        // Build record label with ports
        std::ostringstream label;
        label << "{";

        // Input ports section
        if (!node.inputs.empty()) {
            label << "{";
            for (size_t i = 0; i < node.inputs.size(); ++i) {
                if (i > 0) label << "|";
                label << "<" << DotPortId(node.inputs[i].id) << "> "
                      << node.inputs[i].name;
                if (opts_.show_types) {
                    label << ": " << (node.inputs[i].type.name.empty()
                                          ? "Any"
                                          : node.inputs[i].type.name);
                }
            }
            label << "}|";
        }

        // Node name
        label << "[" << node.language << "] " << node.name;

        // Output ports section
        if (!node.outputs.empty()) {
            label << "|{";
            for (size_t i = 0; i < node.outputs.size(); ++i) {
                if (i > 0) label << "|";
                label << "<" << DotPortId(node.outputs[i].id) << "> "
                      << node.outputs[i].name;
                if (opts_.show_types) {
                    label << ": " << (node.outputs[i].type.name.empty()
                                          ? "Any"
                                          : node.outputs[i].type.name);
                }
            }
            label << "}";
        }

        label << "}";

        out << "  " << nid
            << " [label=\"" << label.str() << "\""
            << ", style=filled, fillcolor=\"" << color << "20\""
            << ", color=\"" << color << "\""
            << "];\n";
    }

    out << "\n";

    // Edges
    for (const auto &edge : graph.Edges()) {
        std::string src = DotNodeId(edge.source_node_id) + ":" + DotPortId(edge.source_port_id);
        std::string tgt = DotNodeId(edge.target_node_id) + ":" + DotPortId(edge.target_port_id);
        std::string color = DotEdgeColor(edge.status);

        out << "  " << src << " -> " << tgt
            << " [color=\"" << color << "\"";
        if (opts_.show_edge_status) {
            std::string label;
            switch (edge.status) {
            case TopologyEdge::Status::kValid:           label = "OK"; break;
            case TopologyEdge::Status::kImplicitConvert: label = "implicit"; break;
            case TopologyEdge::Status::kExplicitConvert: label = "explicit"; break;
            case TopologyEdge::Status::kIncompatible:    label = "ERROR"; break;
            case TopologyEdge::Status::kUnknown:         label = "?"; break;
            }
            out << ", label=\"" << label << "\"";
        }
        out << "];\n";
    }

    out << "}\n";
}

// ============================================================================
// JSON output
// ============================================================================

void TopologyPrinter::PrintJson(const TopologyGraph &graph, std::ostream &out) const {
    out << "{\n";
    out << "  \"module\": \"" << graph.module_name << "\",\n";
    out << "  \"source_file\": \"" << graph.source_file << "\",\n";
    out << "  \"node_count\": " << graph.NodeCount() << ",\n";
    out << "  \"edge_count\": " << graph.EdgeCount() << ",\n";

    // Nodes
    out << "  \"nodes\": [\n";
    for (size_t i = 0; i < graph.Nodes().size(); ++i) {
        const auto &node = graph.Nodes()[i];
        out << "    {\n";
        out << "      \"id\": " << node.id << ",\n";
        out << "      \"name\": \"" << node.name << "\",\n";
        out << "      \"language\": \"" << node.language << "\",\n";

        std::string kind_str;
        switch (node.kind) {
        case TopologyNode::Kind::kFunction:     kind_str = "function"; break;
        case TopologyNode::Kind::kConstructor:  kind_str = "constructor"; break;
        case TopologyNode::Kind::kMethod:       kind_str = "method"; break;
        case TopologyNode::Kind::kPipeline:     kind_str = "pipeline"; break;
        case TopologyNode::Kind::kMapFunc:      kind_str = "map_func"; break;
        case TopologyNode::Kind::kExternalCall: kind_str = "external_call"; break;
        }
        out << "      \"kind\": \"" << kind_str << "\",\n";
        out << "      \"is_linked\": " << (node.is_linked ? "true" : "false") << ",\n";

        // Input ports
        out << "      \"inputs\": [\n";
        for (size_t j = 0; j < node.inputs.size(); ++j) {
            const auto &port = node.inputs[j];
            out << "        {\"id\": " << port.id
                << ", \"name\": \"" << port.name
                << "\", \"type\": \"" << (port.type.name.empty() ? "Any" : port.type.name)
                << "\", \"index\": " << port.index << "}";
            if (j + 1 < node.inputs.size()) out << ",";
            out << "\n";
        }
        out << "      ],\n";

        // Output ports
        out << "      \"outputs\": [\n";
        for (size_t j = 0; j < node.outputs.size(); ++j) {
            const auto &port = node.outputs[j];
            out << "        {\"id\": " << port.id
                << ", \"name\": \"" << port.name
                << "\", \"type\": \"" << (port.type.name.empty() ? "Any" : port.type.name)
                << "\", \"index\": " << port.index << "}";
            if (j + 1 < node.outputs.size()) out << ",";
            out << "\n";
        }
        out << "      ]\n";

        out << "    }";
        if (i + 1 < graph.Nodes().size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // Edges
    out << "  \"edges\": [\n";
    for (size_t i = 0; i < graph.Edges().size(); ++i) {
        const auto &edge = graph.Edges()[i];
        std::string status_str;
        switch (edge.status) {
        case TopologyEdge::Status::kValid:           status_str = "valid"; break;
        case TopologyEdge::Status::kImplicitConvert: status_str = "implicit_convert"; break;
        case TopologyEdge::Status::kExplicitConvert: status_str = "explicit_convert"; break;
        case TopologyEdge::Status::kIncompatible:    status_str = "incompatible"; break;
        case TopologyEdge::Status::kUnknown:         status_str = "unknown"; break;
        }
        out << "    {\"id\": " << edge.id
            << ", \"source_node\": " << edge.source_node_id
            << ", \"source_port\": " << edge.source_port_id
            << ", \"target_node\": " << edge.target_node_id
            << ", \"target_port\": " << edge.target_port_id
            << ", \"status\": \"" << status_str << "\"}";
        if (i + 1 < graph.Edges().size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

// ============================================================================
// Diagnostics output
// ============================================================================

void TopologyPrinter::PrintDiagnostics(
    const std::vector<ValidationDiagnostic> &diagnostics,
    std::ostream &out) const {
    if (diagnostics.empty()) {
        out << ColorString("No validation issues found.", kGreen) << "\n";
        return;
    }

    size_t errors = 0, warnings = 0, infos = 0;
    for (const auto &d : diagnostics) {
        switch (d.severity) {
        case ValidationDiagnostic::Severity::kError:   errors++; break;
        case ValidationDiagnostic::Severity::kWarning: warnings++; break;
        case ValidationDiagnostic::Severity::kInfo:    infos++; break;
        }
    }

    out << ColorString("Validation Results:", kBold) << " "
        << ColorString(std::to_string(errors) + " error(s)", errors > 0 ? kRed : kGreen) << ", "
        << ColorString(std::to_string(warnings) + " warning(s)", warnings > 0 ? kYellow : kGreen) << ", "
        << std::to_string(infos) << " info(s)\n\n";

    for (const auto &diag : diagnostics) {
        std::string prefix;
        std::string color;
        switch (diag.severity) {
        case ValidationDiagnostic::Severity::kError:
            prefix = "ERROR";
            color = kRed;
            break;
        case ValidationDiagnostic::Severity::kWarning:
            prefix = "WARN ";
            color = kYellow;
            break;
        case ValidationDiagnostic::Severity::kInfo:
            prefix = "INFO ";
            color = kBlue;
            break;
        }
        out << ColorString("[" + prefix + "]", color) << " " << diag.message;
        if (diag.loc.line > 0) {
            out << " " << ColorString("(" + diag.loc.file + ":" +
                                           std::to_string(diag.loc.line) + ")",
                                      kGray);
        }
        out << "\n";
    }
}

} // namespace polyglot::tools::topo
