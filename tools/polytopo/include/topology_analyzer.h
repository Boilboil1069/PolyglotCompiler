/**
 * @file     topology_analyzer.h
 * @brief    Builds a TopologyGraph from a .ploy AST
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polytopo/include/topology_graph.h"

namespace polyglot::tools::topo {

// ============================================================================
// TopologyAnalyzer — AST-to-graph builder
// ============================================================================

/** @brief TopologyAnalyzer class. */
class TopologyAnalyzer {
  public:
    // Construct an analyzer with semantic information for type resolution
    explicit TopologyAnalyzer(const ploy::PloySema &sema);
    ~TopologyAnalyzer() = default;

    // Build a topology graph from the given AST module.
    // Returns true on success.  Diagnostics are pushed into the graph's
    // validation list (accessible via Validate()).
    bool Build(const std::shared_ptr<ploy::Module> &module);

    // Access the constructed graph
    const TopologyGraph &Graph() const { return graph_; }
    TopologyGraph &MutableGraph() { return graph_; }

  private:
    // Walk top-level declarations
    void AnalyzeStatement(const std::shared_ptr<ploy::Statement> &stmt);

    // Extract nodes from specific declaration types
    void AnalyzeFuncDecl(const std::shared_ptr<ploy::FuncDecl> &func);
    void AnalyzeLinkDecl(const std::shared_ptr<ploy::LinkDecl> &link);
    void AnalyzePipelineDecl(const std::shared_ptr<ploy::PipelineDecl> &pipeline);
    void AnalyzeMapFuncDecl(const std::shared_ptr<ploy::MapFuncDecl> &map_func);
    void AnalyzeExtendDecl(const std::shared_ptr<ploy::ExtendDecl> &extend);

    // Walk function / pipeline bodies to extract data-flow edges
    void AnalyzeBody(const std::vector<std::shared_ptr<ploy::Statement>> &stmts,
                     uint64_t context_node_id);
    void AnalyzeBodyStatement(const std::shared_ptr<ploy::Statement> &stmt,
                              uint64_t context_node_id);

    // Extract edges from expressions
    /** @brief ExprResult data structure. */
    struct ExprResult {
        uint64_t producer_node_id{0};
        uint64_t producer_port_id{0};
        core::Type type{core::Type::Any()};
    };
    ExprResult AnalyzeExpression(const std::shared_ptr<ploy::Expression> &expr,
                                 uint64_t context_node_id);

    // Helpers for cross-language expressions
    ExprResult AnalyzeCrossLangCall(const std::shared_ptr<ploy::CrossLangCallExpression> &call,
                                   uint64_t context_node_id);
    ExprResult AnalyzeNewExpression(const std::shared_ptr<ploy::NewExpression> &new_expr,
                                   uint64_t context_node_id);
    ExprResult AnalyzeMethodCall(const std::shared_ptr<ploy::MethodCallExpression> &method,
                                 uint64_t context_node_id);
    ExprResult AnalyzeCallExpression(const std::shared_ptr<ploy::CallExpression> &call,
                                     uint64_t context_node_id);

    // Resolve a type from a TypeNode via the sema type system
    core::Type ResolveType(const std::shared_ptr<ploy::TypeNode> &type_node) const;

    // Find or create a node for an external (cross-language) function
    uint64_t FindOrCreateExternalNode(const std::string &language,
                                      const std::string &function_name,
                                      const core::SourceLoc &loc);

    // Connect an expression result to a target node's input port
    void ConnectEdge(const ExprResult &source,
                     uint64_t target_node_id, uint64_t target_port_id,
                     const core::SourceLoc &loc);

    const ploy::PloySema &sema_;
    TopologyGraph graph_;

    // Mapping from variable name to the expression result that produced it
    /** @brief VarBinding data structure. */
    struct VarBinding {
        uint64_t producer_node_id{0};
        uint64_t producer_port_id{0};
        core::Type type{core::Type::Any()};
    };
    std::unordered_map<std::string, VarBinding> var_bindings_;

    // Track external nodes by qualified name to avoid duplicates
    std::unordered_map<std::string, uint64_t> external_nodes_;

    // Current body-analysis context: the FUNC / pipeline-stage node whose
    // body is being walked.  Set before AnalyzeBody() and read by
    // FindOrCreateExternalNode() / ConnectEdge() to tag new items.
    uint64_t current_context_id_{0};
};

} // namespace polyglot::tools::topo
