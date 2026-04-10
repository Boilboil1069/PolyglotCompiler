/**
 * @file     topology_graph.h
 * @brief    Core data structures for the Polyglot topology graph
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/include/core/source_loc.h"
#include "common/include/core/types.h"

namespace polyglot::tools::topo {

// ============================================================================
// Port — a typed input or output slot on a topology node
// ============================================================================

/** @brief Port data structure. */
struct Port {
    /** @brief Direction enumeration. */
    enum class Direction { kInput, kOutput };

    std::string name;                          // Parameter or return-value name
    Direction direction{Direction::kInput};
    core::Type type{core::Type::Any()};        // Semantic type
    std::string language;                      // Owning language (e.g. "cpp", "python")
    int index{0};                              // Positional index within the node

    // Unique id generated at graph-build time
    uint64_t id{0};

    bool operator==(const Port &o) const { return id == o.id; }
    bool operator!=(const Port &o) const { return id != o.id; }
};

// ============================================================================
// TopologyNode — a function / class-ctor / method in the topology graph
// ============================================================================

/** @brief TopologyNode data structure. */
struct TopologyNode {
    /** @brief Kind enumeration. */
    enum class Kind {
        kFunction,      // Standalone function (FUNC / CALL / LINK target)
        kConstructor,   // NEW(...) class constructor
        kMethod,        // METHOD(...) call on an object
        kPipeline,      // PIPELINE block
        kMapFunc,       // MAP_FUNC type conversion helper
        kExternalCall,  // Cross-language CALL(lang, func, ...)
    };

    uint64_t id{0};
    std::string name;                       // Qualified name (e.g. "cpp::image::enhance")
    std::string language;                   // Source language
    Kind kind{Kind::kFunction};

    std::vector<Port> inputs;               // Input ports (parameters)
    std::vector<Port> outputs;              // Output ports (return values)

    core::SourceLoc loc{};                  // Source location in .ploy file

    // Optional metadata
    std::string description;                // Human-readable description
    bool is_linked{false};                  // True if the node is a LINK target
    std::string link_source_language;       // Source language of linked function
    std::string link_source_function;       // Source function of linked function

    // Origin: where this node was primarily created from
    /** @brief Origin enumeration. */
    enum class Origin {
        kLink,              // Created from a LINK declaration
        kCall,              // Created from a FUNC/PIPELINE body CALL
        kDecl,              // Created from a top-level declaration (FUNC, PIPELINE, etc.)
        kPipelineStage,     // Created as a stage inside a PIPELINE body
    };
    Origin origin{Origin::kDecl};
};

// ============================================================================
// TopologyEdge — a connection between an output port and an input port
// ============================================================================

/** @brief TopologyEdge data structure. */
struct TopologyEdge {
    uint64_t id{0};

    uint64_t source_node_id{0};
    uint64_t source_port_id{0};   // Output port on the source node

    uint64_t target_node_id{0};
    uint64_t target_port_id{0};   // Input port on the target node

    // Type compatibility status
    /** @brief Status enumeration. */
    enum class Status {
        kValid,             // Types are directly compatible
        kImplicitConvert,   // Requires implicit type conversion (widening, etc.)
        kExplicitConvert,   // Requires explicit MAP_TYPE / CONVERT
        kIncompatible,      // Types are incompatible — validation error
        kUnknown,           // One or both types are Any/unresolved
    };
    Status status{Status::kUnknown};

    // Origin: where this edge was created from
    /** @brief Origin enumeration. */
    enum class Origin {
        kLink,              // Created from a LINK declaration (binding-level)
        kCall,              // Created from a FUNC/PIPELINE CALL (data-flow-level)
        kPipelineStage,     // Created from sequential stage ordering in a PIPELINE
    };
    Origin origin{Origin::kCall};

    // If conversion is needed, the required marshal operation
    std::string conversion_note;

    core::SourceLoc loc{};         // Source location of the connection
};

// ============================================================================
// ValidationDiagnostic — a diagnostic produced during topology validation
// ============================================================================

/** @brief ValidationDiagnostic data structure. */
struct ValidationDiagnostic {
    /** @brief Severity enumeration. */
    enum class Severity { kError, kWarning, kInfo };

    Severity severity{Severity::kError};
    std::string message;
    core::SourceLoc loc{};

    // Related nodes/edges for traceback
    uint64_t node_id{0};
    uint64_t edge_id{0};
    uint64_t port_id{0};
};

// ============================================================================
// TopologyGraph — the complete topology graph for a .ploy module
// ============================================================================

/** @brief TopologyGraph class. */
class TopologyGraph {
  public:
    TopologyGraph() = default;
    ~TopologyGraph() = default;

    // -- Node management -----------------------------------------------------

    // Add a node and return its assigned id
    uint64_t AddNode(TopologyNode node);

    // Look up a node by id (returns nullptr if not found)
    const TopologyNode *GetNode(uint64_t id) const;
    TopologyNode *GetMutableNode(uint64_t id);

    // Find a node by qualified name (returns nullptr if not found)
    const TopologyNode *FindNodeByName(const std::string &name) const;

    // All nodes (ordered by insertion)
    const std::vector<TopologyNode> &Nodes() const { return nodes_; }

    // -- Edge management -----------------------------------------------------

    // Add an edge connecting two ports; returns edge id
    uint64_t AddEdge(TopologyEdge edge);

    // All edges
    const std::vector<TopologyEdge> &Edges() const { return edges_; }

    // Edges originating from a given node
    std::vector<const TopologyEdge *> OutEdges(uint64_t node_id) const;

    // Edges targeting a given node
    std::vector<const TopologyEdge *> InEdges(uint64_t node_id) const;

    // -- Query ---------------------------------------------------------------

    // Return all root nodes (nodes with no incoming edges)
    std::vector<const TopologyNode *> Roots() const;

    // Return all leaf nodes (nodes with no outgoing edges)
    std::vector<const TopologyNode *> Leaves() const;

    // Topological sort (returns empty vector if cycle detected)
    std::vector<const TopologyNode *> TopologicalSort() const;

    // Detect cycles (returns list of node ids forming a cycle, empty if acyclic)
    std::vector<uint64_t> DetectCycles() const;

    // -- Statistics ----------------------------------------------------------

    size_t NodeCount() const { return nodes_.size(); }
    size_t EdgeCount() const { return edges_.size(); }

    // Count of nodes per language
    std::unordered_map<std::string, size_t> LanguageDistribution() const;

    // Count of edges per status
    std::unordered_map<TopologyEdge::Status, size_t> EdgeStatusDistribution() const;

    // -- Metadata ------------------------------------------------------------

    std::string module_name;       // Name of the .ploy module
    std::string source_file;       // Path to the .ploy file

  private:
    uint64_t next_node_id_{1};
    uint64_t next_edge_id_{1};
    uint64_t next_port_id_{1};

    std::vector<TopologyNode> nodes_;
    std::vector<TopologyEdge> edges_;

    // Index: node id -> index in nodes_
    std::unordered_map<uint64_t, size_t> node_index_;
    // Index: qualified name -> node id
    std::unordered_map<std::string, uint64_t> name_index_;

    // Allow the analyzer to assign port ids
    friend class TopologyAnalyzer;
    uint64_t AllocPortId() { return next_port_id_++; }
};

} // namespace polyglot::tools::topo
