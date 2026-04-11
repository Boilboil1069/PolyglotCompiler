/**
 * @file     topology_analyzer.cpp
 * @brief    Builds a TopologyGraph from a .ploy AST
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/polytopo/include/topology_analyzer.h"

#include <algorithm>
#include <cassert>

namespace polyglot::tools::topo {

// ============================================================================
// Construction
// ============================================================================

TopologyAnalyzer::TopologyAnalyzer(const ploy::PloySema &sema)
    : sema_(sema) {}

// ============================================================================
// Public API
// ============================================================================

bool TopologyAnalyzer::Build(const std::shared_ptr<ploy::Module> &module) {
    if (!module) return false;

    graph_.module_name = module->filename;
    graph_.source_file = module->filename;

    // First pass: register all top-level declarations as nodes
    for (const auto &stmt : module->declarations) {
        AnalyzeStatement(stmt);
    }

    // Second pass: walk function/pipeline bodies to find data-flow edges.
    // For each body, register the function's parameters into var_bindings_
    // so that CALL argument references (e.g. `a`, `b`) can be traced back
    // to the FUNC node's input ports, producing correct data-flow edges.
    for (const auto &stmt : module->declarations) {
        if (auto func = std::dynamic_pointer_cast<ploy::FuncDecl>(stmt)) {
            auto *node = graph_.FindNodeByName(func->name);
            if (node) {
                // Save and reset var_bindings_ per function scope
                auto saved_bindings = std::move(var_bindings_);
                var_bindings_.clear();
                current_context_id_ = node->id;

                // Bind each FUNC parameter to the node's input port
                for (size_t i = 0; i < func->params.size() && i < node->inputs.size(); ++i) {
                    var_bindings_[func->params[i].name] = {
                        node->id, node->inputs[i].id, node->inputs[i].type};
                }

                AnalyzeBody(func->body, node->id);

                current_context_id_ = 0;
                var_bindings_ = std::move(saved_bindings);
            }
        } else if (auto pipeline = std::dynamic_pointer_cast<ploy::PipelineDecl>(stmt)) {
            auto *node = graph_.FindNodeByName("pipeline:" + pipeline->name);
            if (node) {
                auto saved_bindings = std::move(var_bindings_);
                var_bindings_.clear();
                current_context_id_ = node->id;
                AnalyzeBody(pipeline->body, node->id);
                current_context_id_ = 0;
                var_bindings_ = std::move(saved_bindings);
            }

            // Analyze each FUNC stage inside the pipeline body
            for (const auto &body_stmt : pipeline->body) {
                auto sub_func = std::dynamic_pointer_cast<ploy::FuncDecl>(body_stmt);
                if (!sub_func) continue;
                auto *stage_node = graph_.FindNodeByName(
                    "pipeline:" + pipeline->name + "::" + sub_func->name);
                if (!stage_node) continue;

                auto saved_bindings2 = std::move(var_bindings_);
                var_bindings_.clear();
                current_context_id_ = stage_node->id;
                for (size_t i = 0; i < sub_func->params.size()
                                   && i < stage_node->inputs.size(); ++i) {
                    var_bindings_[sub_func->params[i].name] = {
                        stage_node->id, stage_node->inputs[i].id,
                        stage_node->inputs[i].type};
                }
                AnalyzeBody(sub_func->body, stage_node->id);
                current_context_id_ = 0;
                var_bindings_ = std::move(saved_bindings2);
            }
        } else if (auto map_func = std::dynamic_pointer_cast<ploy::MapFuncDecl>(stmt)) {
            auto *node = graph_.FindNodeByName("map:" + map_func->name);
            if (node) {
                auto saved_bindings = std::move(var_bindings_);
                var_bindings_.clear();
                current_context_id_ = node->id;

                for (size_t i = 0; i < map_func->params.size() && i < node->inputs.size(); ++i) {
                    var_bindings_[map_func->params[i].name] = {
                        node->id, node->inputs[i].id, node->inputs[i].type};
                }

                AnalyzeBody(map_func->body, node->id);
                current_context_id_ = 0;
                var_bindings_ = std::move(saved_bindings);
            }
        }
    }

    return true;
}

// ============================================================================
// Top-level statement analysis (first pass — node creation)
// ============================================================================

void TopologyAnalyzer::AnalyzeStatement(const std::shared_ptr<ploy::Statement> &stmt) {
    if (auto func = std::dynamic_pointer_cast<ploy::FuncDecl>(stmt)) {
        AnalyzeFuncDecl(func);
    } else if (auto link = std::dynamic_pointer_cast<ploy::LinkDecl>(stmt)) {
        AnalyzeLinkDecl(link);
    } else if (auto pipeline = std::dynamic_pointer_cast<ploy::PipelineDecl>(stmt)) {
        AnalyzePipelineDecl(pipeline);
    } else if (auto map_func = std::dynamic_pointer_cast<ploy::MapFuncDecl>(stmt)) {
        AnalyzeMapFuncDecl(map_func);
    } else if (auto extend = std::dynamic_pointer_cast<ploy::ExtendDecl>(stmt)) {
        AnalyzeExtendDecl(extend);
    }
    // Import, Export, VarDecl, VenvConfig, etc. do not produce nodes
}

void TopologyAnalyzer::AnalyzeFuncDecl(const std::shared_ptr<ploy::FuncDecl> &func) {
    TopologyNode node;
    node.name = func->name;
    node.language = "ploy";
    node.kind = TopologyNode::Kind::kFunction;
    node.loc = func->loc;

    // Input ports from parameters
    for (size_t i = 0; i < func->params.size(); ++i) {
        Port port;
        port.name = func->params[i].name;
        port.direction = Port::Direction::kInput;
        port.type = ResolveType(func->params[i].type);
        port.language = "ploy";
        port.index = static_cast<int>(i);
        node.inputs.push_back(std::move(port));
    }

    // Output port from return type
    Port ret_port;
    ret_port.name = "return";
    ret_port.direction = Port::Direction::kOutput;
    ret_port.type = func->return_type ? ResolveType(func->return_type) : core::Type::Void();
    ret_port.language = "ploy";
    ret_port.index = 0;
    node.outputs.push_back(std::move(ret_port));

    graph_.AddNode(std::move(node));
}

void TopologyAnalyzer::AnalyzeLinkDecl(const std::shared_ptr<ploy::LinkDecl> &link) {
    // A LINK creates a connection between target and source functions.
    // We create nodes for both sides if they don't already exist.

    // Target function node
    std::string target_qualified = link->target_language + "::" + link->target_symbol;
    if (!graph_.FindNodeByName(target_qualified)) {
        TopologyNode target_node;
        target_node.name = target_qualified;
        target_node.language = link->target_language;
        target_node.kind = TopologyNode::Kind::kExternalCall;
        target_node.loc = link->loc;
        target_node.is_linked = true;
        target_node.link_source_language = link->source_language;
        target_node.link_source_function = link->source_symbol;
        target_node.origin = TopologyNode::Origin::kLink;

        // Try to get signature from sema (registered under target_symbol)
        auto sig_it = sema_.KnownSignatures().find(link->target_symbol);
        const ploy::FunctionSignature *sig = (sig_it != sema_.KnownSignatures().end()) ? &sig_it->second : nullptr;
        if (sig && !sig->param_types.empty()) {
            for (size_t i = 0; i < sig->param_types.size(); ++i) {
                Port port;
                port.name = i < sig->param_names.size() ? sig->param_names[i]
                                                        : "arg" + std::to_string(i);
                port.direction = Port::Direction::kInput;
                port.type = sig->param_types[i];
                port.language = link->target_language;
                port.index = static_cast<int>(i);
                target_node.inputs.push_back(std::move(port));
            }
            Port ret_port;
            ret_port.name = "return";
            ret_port.direction = Port::Direction::kOutput;
            ret_port.type = sig->return_type;
            ret_port.language = link->target_language;
            ret_port.index = 0;
            target_node.outputs.push_back(std::move(ret_port));
        } else {
            // Unknown signature: single Any input, Any output
            Port in;
            in.name = "args";
            in.direction = Port::Direction::kInput;
            in.type = core::Type::Any();
            in.language = link->target_language;
            in.index = 0;
            target_node.inputs.push_back(std::move(in));

            Port out;
            out.name = "return";
            out.direction = Port::Direction::kOutput;
            out.type = core::Type::Any();
            out.language = link->target_language;
            out.index = 0;
            target_node.outputs.push_back(std::move(out));
        }

        graph_.AddNode(std::move(target_node));
    }

    // Source function node (the function providing data)
    std::string source_qualified = link->source_language + "::" + link->source_symbol;
    if (!graph_.FindNodeByName(source_qualified)) {
        TopologyNode source_node;
        source_node.name = source_qualified;
        source_node.language = link->source_language;
        source_node.kind = TopologyNode::Kind::kExternalCall;
        source_node.loc = link->loc;
        source_node.origin = TopologyNode::Origin::kLink;

        auto src_sig_it = sema_.KnownSignatures().find(link->source_symbol);
        const ploy::FunctionSignature *sig = (src_sig_it != sema_.KnownSignatures().end()) ? &src_sig_it->second : nullptr;
        if (sig && !sig->param_types.empty()) {
            for (size_t i = 0; i < sig->param_types.size(); ++i) {
                Port port;
                port.name = i < sig->param_names.size() ? sig->param_names[i]
                                                        : "arg" + std::to_string(i);
                port.direction = Port::Direction::kInput;
                port.type = sig->param_types[i];
                port.language = link->source_language;
                port.index = static_cast<int>(i);
                source_node.inputs.push_back(std::move(port));
            }
            Port ret_port;
            ret_port.name = "return";
            ret_port.direction = Port::Direction::kOutput;
            ret_port.type = sig->return_type;
            ret_port.language = link->source_language;
            ret_port.index = 0;
            source_node.outputs.push_back(std::move(ret_port));
        } else {
            Port in;
            in.name = "args";
            in.direction = Port::Direction::kInput;
            in.type = core::Type::Any();
            in.language = link->source_language;
            in.index = 0;
            source_node.inputs.push_back(std::move(in));

            Port out;
            out.name = "return";
            out.direction = Port::Direction::kOutput;
            out.type = core::Type::Any();
            out.language = link->source_language;
            out.index = 0;
            source_node.outputs.push_back(std::move(out));
        }

        graph_.AddNode(std::move(source_node));
    }

    // Create an edge from source output to target input (data flow direction)
    const auto *src_node = graph_.FindNodeByName(source_qualified);
    const auto *tgt_node = graph_.FindNodeByName(target_qualified);
    if (src_node && tgt_node && !src_node->outputs.empty() && !tgt_node->inputs.empty()) {
        TopologyEdge edge;
        edge.source_node_id = src_node->id;
        edge.source_port_id = src_node->outputs[0].id;
        edge.target_node_id = tgt_node->id;
        edge.target_port_id = tgt_node->inputs[0].id;
        edge.origin = TopologyEdge::Origin::kLink;
        edge.loc = link->loc;
        // Determine edge status based on available type information
        bool has_map_type = !link->body.empty();
        if (has_map_type) {
            edge.status = TopologyEdge::Status::kExplicitConvert;
            edge.conversion_note = "MAP_TYPE";
        } else if (link->target_language == link->source_language) {
            edge.status = TopologyEdge::Status::kValid;
        } else {
            edge.status = TopologyEdge::Status::kUnknown;
        }
        graph_.AddEdge(std::move(edge));
    }
}

void TopologyAnalyzer::AnalyzePipelineDecl(const std::shared_ptr<ploy::PipelineDecl> &pipeline) {
    // Create the pipeline container node (declaration-level, origin=kDecl)
    TopologyNode node;
    node.name = "pipeline:" + pipeline->name;
    node.language = "ploy";
    node.kind = TopologyNode::Kind::kPipeline;
    node.loc = pipeline->loc;

    // Pipeline has a single output (the result of the pipeline)
    Port out;
    out.name = "result";
    out.direction = Port::Direction::kOutput;
    out.type = core::Type::Any();
    out.language = "ploy";
    out.index = 0;
    node.outputs.push_back(std::move(out));

    graph_.AddNode(std::move(node));

    // --- Stage sub-nodes ---------------------------------------------------
    // Collect FUNC declarations from the PIPELINE body.  Each FUNC becomes a
    // stage node (origin=kPipelineStage) so that the PIPELINE view displays
    // the internal execution order and data-flow direction.

    struct StageInfo {
        uint64_t node_id{0};
    };
    std::vector<StageInfo> stages;

    for (const auto &stmt : pipeline->body) {
        auto func = std::dynamic_pointer_cast<ploy::FuncDecl>(stmt);
        if (!func) continue;

        TopologyNode stage;
        stage.name = "pipeline:" + pipeline->name + "::" + func->name;
        stage.language = "ploy";
        stage.kind = TopologyNode::Kind::kFunction;
        stage.loc = func->loc;
        stage.origin = TopologyNode::Origin::kPipelineStage;  // visible in PIPELINE view

        // Input ports from parameters
        for (size_t i = 0; i < func->params.size(); ++i) {
            Port port;
            port.name = func->params[i].name;
            port.direction = Port::Direction::kInput;
            port.type = ResolveType(func->params[i].type);
            port.language = "ploy";
            port.index = static_cast<int>(i);
            stage.inputs.push_back(std::move(port));
        }

        // Output port from return type
        Port ret_port;
        ret_port.name = "return";
        ret_port.direction = Port::Direction::kOutput;
        ret_port.type = func->return_type ? ResolveType(func->return_type)
                                          : core::Type::Void();
        ret_port.language = "ploy";
        ret_port.index = 0;
        stage.outputs.push_back(std::move(ret_port));

        uint64_t stage_id = graph_.AddNode(std::move(stage));
        stages.push_back({stage_id});
    }

    // Create sequential edges between consecutive stages to represent the
    // pipeline execution order and data-flow direction.
    // Edge: stage[i].output[0] → stage[i+1].input[0]
    for (size_t i = 0; i + 1 < stages.size(); ++i) {
        const auto *src = graph_.GetNode(stages[i].node_id);
        const auto *tgt = graph_.GetNode(stages[i + 1].node_id);
        if (!src || !tgt || src->outputs.empty() || tgt->inputs.empty()) continue;

        TopologyEdge edge;
        edge.source_node_id = src->id;
        edge.source_port_id = src->outputs[0].id;
        edge.target_node_id = tgt->id;
        edge.target_port_id = tgt->inputs[0].id;
        edge.origin = TopologyEdge::Origin::kPipelineStage;
        edge.status = TopologyEdge::Status::kValid;
        edge.loc = tgt->loc;
        edge.conversion_note = "pipeline_stage_order";
        graph_.AddEdge(std::move(edge));
    }
}

void TopologyAnalyzer::AnalyzeMapFuncDecl(const std::shared_ptr<ploy::MapFuncDecl> &map_func) {
    TopologyNode node;
    node.name = "map:" + map_func->name;
    node.language = "ploy";
    node.kind = TopologyNode::Kind::kMapFunc;
    node.loc = map_func->loc;

    for (size_t i = 0; i < map_func->params.size(); ++i) {
        Port port;
        port.name = map_func->params[i].name;
        port.direction = Port::Direction::kInput;
        port.type = ResolveType(map_func->params[i].type);
        port.language = "ploy";
        port.index = static_cast<int>(i);
        node.inputs.push_back(std::move(port));
    }

    Port ret;
    ret.name = "return";
    ret.direction = Port::Direction::kOutput;
    ret.type = map_func->return_type ? ResolveType(map_func->return_type)
                                     : core::Type::Any();
    ret.language = "ploy";
    ret.index = 0;
    node.outputs.push_back(std::move(ret));

    graph_.AddNode(std::move(node));
}

void TopologyAnalyzer::AnalyzeExtendDecl(const std::shared_ptr<ploy::ExtendDecl> &extend) {
    // Create a node for the extended class constructor
    std::string qualified = extend->language + "::" + extend->base_class;
    TopologyNode node;
    node.name = qualified + "::" + extend->derived_name;
    node.language = extend->language;
    node.kind = TopologyNode::Kind::kConstructor;
    node.loc = extend->loc;

    Port out;
    out.name = "instance";
    out.direction = Port::Direction::kOutput;
    out.type = core::Type{core::TypeKind::kClass, extend->derived_name};
    out.language = extend->language;
    out.index = 0;
    node.outputs.push_back(std::move(out));

    graph_.AddNode(std::move(node));

    // Create nodes for overridden methods
    for (const auto &method_stmt : extend->methods) {
        if (auto method = std::dynamic_pointer_cast<ploy::FuncDecl>(method_stmt)) {
            TopologyNode method_node;
            method_node.name = qualified + "::" + extend->derived_name + "::" + method->name;
            method_node.language = extend->language;
            method_node.kind = TopologyNode::Kind::kMethod;
            method_node.loc = method->loc;

            // Self parameter (implicit)
            Port self_port;
            self_port.name = "self";
            self_port.direction = Port::Direction::kInput;
            self_port.type = core::Type{core::TypeKind::kClass, extend->derived_name};
            self_port.language = extend->language;
            self_port.index = 0;
            method_node.inputs.push_back(std::move(self_port));

            for (size_t i = 0; i < method->params.size(); ++i) {
                Port port;
                port.name = method->params[i].name;
                port.direction = Port::Direction::kInput;
                port.type = ResolveType(method->params[i].type);
                port.language = extend->language;
                port.index = static_cast<int>(i + 1);
                method_node.inputs.push_back(std::move(port));
            }

            Port ret;
            ret.name = "return";
            ret.direction = Port::Direction::kOutput;
            ret.type = method->return_type ? ResolveType(method->return_type)
                                           : core::Type::Void();
            ret.language = extend->language;
            ret.index = 0;
            method_node.outputs.push_back(std::move(ret));

            graph_.AddNode(std::move(method_node));
        }
    }
}

// ============================================================================
// Body analysis (second pass — edge creation)
// ============================================================================

void TopologyAnalyzer::AnalyzeBody(
    const std::vector<std::shared_ptr<ploy::Statement>> &stmts,
    uint64_t context_node_id) {
    for (const auto &stmt : stmts) {
        AnalyzeBodyStatement(stmt, context_node_id);
    }
}

void TopologyAnalyzer::AnalyzeBodyStatement(
    const std::shared_ptr<ploy::Statement> &stmt,
    uint64_t context_node_id) {
    if (auto var = std::dynamic_pointer_cast<ploy::VarDecl>(stmt)) {
        // Variable declaration: track which node/port produced the value
        if (var->init) {
            auto result = AnalyzeExpression(var->init, context_node_id);
            var_bindings_[var->name] = {result.producer_node_id,
                                        result.producer_port_id,
                                        result.type};
        }
    } else if (auto expr_stmt = std::dynamic_pointer_cast<ploy::ExprStatement>(stmt)) {
        if (expr_stmt->expr) {
            AnalyzeExpression(expr_stmt->expr, context_node_id);
        }
    } else if (auto ret = std::dynamic_pointer_cast<ploy::ReturnStatement>(stmt)) {
        if (ret->value) {
            auto result = AnalyzeExpression(ret->value, context_node_id);
            // Connect the return value to the context node's output port
            auto *ctx = graph_.GetNode(context_node_id);
            if (ctx && !ctx->outputs.empty()) {
                ConnectEdge(result, context_node_id, ctx->outputs[0].id, ret->loc);
            }
        }
    } else if (auto if_stmt = std::dynamic_pointer_cast<ploy::IfStatement>(stmt)) {
        if (if_stmt->condition) {
            AnalyzeExpression(if_stmt->condition, context_node_id);
        }
        AnalyzeBody(if_stmt->then_body, context_node_id);
        AnalyzeBody(if_stmt->else_body, context_node_id);
    } else if (auto while_stmt = std::dynamic_pointer_cast<ploy::WhileStatement>(stmt)) {
        if (while_stmt->condition) {
            AnalyzeExpression(while_stmt->condition, context_node_id);
        }
        AnalyzeBody(while_stmt->body, context_node_id);
    } else if (auto for_stmt = std::dynamic_pointer_cast<ploy::ForStatement>(stmt)) {
        if (for_stmt->iterable) {
            AnalyzeExpression(for_stmt->iterable, context_node_id);
        }
        AnalyzeBody(for_stmt->body, context_node_id);
    } else if (auto with_stmt = std::dynamic_pointer_cast<ploy::WithStatement>(stmt)) {
        if (with_stmt->resource_expr) {
            auto result = AnalyzeExpression(with_stmt->resource_expr, context_node_id);
            var_bindings_[with_stmt->var_name] = {result.producer_node_id,
                                                   result.producer_port_id,
                                                   result.type};
        }
        AnalyzeBody(with_stmt->body, context_node_id);
    }
}

// ============================================================================
// Expression analysis (data-flow edge extraction)
// ============================================================================

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeExpression(
    const std::shared_ptr<ploy::Expression> &expr,
    uint64_t context_node_id) {
    if (!expr) return {};

    if (auto call = std::dynamic_pointer_cast<ploy::CrossLangCallExpression>(expr)) {
        return AnalyzeCrossLangCall(call, context_node_id);
    } else if (auto new_expr = std::dynamic_pointer_cast<ploy::NewExpression>(expr)) {
        return AnalyzeNewExpression(new_expr, context_node_id);
    } else if (auto method = std::dynamic_pointer_cast<ploy::MethodCallExpression>(expr)) {
        return AnalyzeMethodCall(method, context_node_id);
    } else if (auto call_expr = std::dynamic_pointer_cast<ploy::CallExpression>(expr)) {
        return AnalyzeCallExpression(call_expr, context_node_id);
    } else if (auto ident = std::dynamic_pointer_cast<ploy::Identifier>(expr)) {
        // Variable reference: look up in bindings
        auto it = var_bindings_.find(ident->name);
        if (it != var_bindings_.end()) {
            return {it->second.producer_node_id, it->second.producer_port_id,
                    it->second.type};
        }
        return {};
    } else if (auto binary = std::dynamic_pointer_cast<ploy::BinaryExpression>(expr)) {
        AnalyzeExpression(binary->left, context_node_id);
        AnalyzeExpression(binary->right, context_node_id);
        return {};
    } else if (auto unary = std::dynamic_pointer_cast<ploy::UnaryExpression>(expr)) {
        return AnalyzeExpression(unary->operand, context_node_id);
    }

    // Literals and other expressions do not produce topology edges
    return {};
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeCrossLangCall(
    const std::shared_ptr<ploy::CrossLangCallExpression> &call,
    uint64_t context_node_id) {
    std::string qualified = call->language + "::" + call->function;
    uint64_t callee_id = FindOrCreateExternalNode(call->language, call->function, call->loc);

    auto *callee = graph_.GetNode(callee_id);
    if (!callee) return {};

    // Connect each argument to the callee's input ports
    for (size_t i = 0; i < call->args.size(); ++i) {
        auto arg_result = AnalyzeExpression(call->args[i], context_node_id);
        if (arg_result.producer_node_id != 0 && i < callee->inputs.size()) {
            ConnectEdge(arg_result, callee_id, callee->inputs[i].id, call->loc);
        }
    }

    // Return the callee's output port as the expression result
    if (!callee->outputs.empty()) {
        return {callee_id, callee->outputs[0].id, callee->outputs[0].type};
    }
    return {callee_id, 0, core::Type::Any()};
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeNewExpression(
    const std::shared_ptr<ploy::NewExpression> &new_expr,
    uint64_t context_node_id) {
    std::string qualified = new_expr->language + "::" + new_expr->class_name + "::new";
    uint64_t ctor_id = FindOrCreateExternalNode(new_expr->language,
                                                 new_expr->class_name + "::new",
                                                 new_expr->loc);

    auto *ctor = graph_.GetMutableNode(ctor_id);
    if (ctor) {
        ctor->kind = TopologyNode::Kind::kConstructor;

        // Connect constructor arguments
        for (size_t i = 0; i < new_expr->args.size(); ++i) {
            auto arg_result = AnalyzeExpression(new_expr->args[i], context_node_id);
            if (arg_result.producer_node_id != 0 && i < ctor->inputs.size()) {
                ConnectEdge(arg_result, ctor_id, ctor->inputs[i].id, new_expr->loc);
            }
        }

        // The constructor output is a class instance
        if (!ctor->outputs.empty()) {
            return {ctor_id, ctor->outputs[0].id,
                    core::Type{core::TypeKind::kClass, new_expr->class_name}};
        }
    }
    return {ctor_id, 0, core::Type{core::TypeKind::kClass, new_expr->class_name}};
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeMethodCall(
    const std::shared_ptr<ploy::MethodCallExpression> &method,
    uint64_t context_node_id) {
    // Resolve the object (first argument)
    auto obj_result = AnalyzeExpression(method->object, context_node_id);

    std::string qualified = method->language + "::method::" + method->method_name;
    uint64_t method_id = FindOrCreateExternalNode(method->language,
                                                   "method::" + method->method_name,
                                                   method->loc);

    auto *method_node = graph_.GetMutableNode(method_id);
    if (method_node) {
        method_node->kind = TopologyNode::Kind::kMethod;

        // Connect object as first input
        if (obj_result.producer_node_id != 0 && !method_node->inputs.empty()) {
            ConnectEdge(obj_result, method_id, method_node->inputs[0].id, method->loc);
        }

        // Connect method arguments
        for (size_t i = 0; i < method->args.size(); ++i) {
            auto arg_result = AnalyzeExpression(method->args[i], context_node_id);
            if (arg_result.producer_node_id != 0 && (i + 1) < method_node->inputs.size()) {
                ConnectEdge(arg_result, method_id, method_node->inputs[i + 1].id,
                            method->loc);
            }
        }

        if (!method_node->outputs.empty()) {
            return {method_id, method_node->outputs[0].id,
                    method_node->outputs[0].type};
        }
    }
    return {method_id, 0, core::Type::Any()};
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeCallExpression(
    const std::shared_ptr<ploy::CallExpression> &call,
    uint64_t context_node_id) {
    // Resolve callee name
    std::string callee_name;
    if (auto ident = std::dynamic_pointer_cast<ploy::Identifier>(call->callee)) {
        callee_name = ident->name;
    } else if (auto qident = std::dynamic_pointer_cast<ploy::QualifiedIdentifier>(call->callee)) {
        callee_name = qident->qualifier + "::" + qident->name;
    }

    if (callee_name.empty()) return {};

    // Find the callee node in the graph
    const auto *callee = graph_.FindNodeByName(callee_name);
    if (!callee) return {};

    // Connect arguments to the callee's input ports
    for (size_t i = 0; i < call->args.size(); ++i) {
        auto arg_result = AnalyzeExpression(call->args[i], context_node_id);
        if (arg_result.producer_node_id != 0 && i < callee->inputs.size()) {
            ConnectEdge(arg_result, callee->id, callee->inputs[i].id, call->loc);
        }
    }

    if (!callee->outputs.empty()) {
        return {callee->id, callee->outputs[0].id, callee->outputs[0].type};
    }
    return {callee->id, 0, core::Type::Any()};
}

// ============================================================================
// Helpers
// ============================================================================

core::Type TopologyAnalyzer::ResolveType(
    const std::shared_ptr<ploy::TypeNode> &type_node) const {
    if (!type_node) return core::Type::Any();

    if (auto simple = std::dynamic_pointer_cast<ploy::SimpleType>(type_node)) {
        if (simple->name == "INT" || simple->name == "int") return core::Type::Int();
        if (simple->name == "FLOAT" || simple->name == "float") return core::Type::Float();
        if (simple->name == "BOOL" || simple->name == "bool") return core::Type::Bool();
        if (simple->name == "STRING" || simple->name == "string") return core::Type::String();
        if (simple->name == "VOID" || simple->name == "void") return core::Type::Void();
        return core::Type::Any();
    } else if (auto qualified = std::dynamic_pointer_cast<ploy::QualifiedType>(type_node)) {
        core::Type t;
        t.kind = core::TypeKind::kClass;
        t.name = qualified->type_name;
        t.language = qualified->language;
        return t;
    }
    return core::Type::Any();
}

uint64_t TopologyAnalyzer::FindOrCreateExternalNode(
    const std::string &language,
    const std::string &function_name,
    const core::SourceLoc &loc) {
    std::string qualified = language + "::" + function_name;

    // Check if already exists
    auto it = external_nodes_.find(qualified);
    if (it != external_nodes_.end()) {
        return it->second;
    }

    // Also check graph by name
    const auto *existing = graph_.FindNodeByName(qualified);
    if (existing) {
        external_nodes_[qualified] = existing->id;
        return existing->id;
    }

    // Create a new external node
    TopologyNode node;
    node.name = qualified;
    node.language = language;
    node.kind = TopologyNode::Kind::kExternalCall;
    node.loc = loc;
    node.origin = TopologyNode::Origin::kCall;
    node.context_node_id = current_context_id_;

    // Try to resolve signature from sema.
    // The signature may be registered under different key formats:
    //   - qualified with language: "cpp::math_ops::add"
    //   - without language prefix: "math_ops::add"
    //   - short name only: "add"
    const ploy::FunctionSignature *sig = nullptr;
    auto ext_sig_it = sema_.KnownSignatures().find(qualified);
    if (ext_sig_it != sema_.KnownSignatures().end()) {
        sig = &ext_sig_it->second;
    }
    if (!sig || !sig->param_count_known) {
        // Try without language prefix: "math_ops::add"
        ext_sig_it = sema_.KnownSignatures().find(function_name);
        if (ext_sig_it != sema_.KnownSignatures().end()) {
            sig = &ext_sig_it->second;
        }
    }
    if (!sig || !sig->param_count_known) {
        // Try short name: "add" (last component after ::)
        auto last_sep = function_name.rfind("::");
        if (last_sep != std::string::npos) {
            std::string short_name = function_name.substr(last_sep + 2);
            ext_sig_it = sema_.KnownSignatures().find(short_name);
            if (ext_sig_it != sema_.KnownSignatures().end()) {
                sig = &ext_sig_it->second;
            }
        }
    }
    if (sig && sig->param_count_known) {
        for (size_t i = 0; i < sig->param_types.size(); ++i) {
            Port port;
            port.name = i < sig->param_names.size() ? sig->param_names[i]
                                                    : "arg" + std::to_string(i);
            port.direction = Port::Direction::kInput;
            port.type = sig->param_types[i];
            port.language = language;
            port.index = static_cast<int>(i);
            node.inputs.push_back(std::move(port));
        }
        Port ret_port;
        ret_port.name = "return";
        ret_port.direction = Port::Direction::kOutput;
        ret_port.type = sig->return_type;
        ret_port.language = language;
        ret_port.index = 0;
        node.outputs.push_back(std::move(ret_port));
    } else {
        // Unknown signature: single variadic input + single output
        Port in;
        in.name = "args";
        in.direction = Port::Direction::kInput;
        in.type = core::Type::Any();
        in.language = language;
        in.index = 0;
        node.inputs.push_back(std::move(in));

        Port out;
        out.name = "return";
        out.direction = Port::Direction::kOutput;
        out.type = core::Type::Any();
        out.language = language;
        out.index = 0;
        node.outputs.push_back(std::move(out));
    }

    uint64_t id = graph_.AddNode(std::move(node));
    external_nodes_[qualified] = id;
    return id;
}

void TopologyAnalyzer::ConnectEdge(
    const ExprResult &source,
    uint64_t target_node_id,
    uint64_t target_port_id,
    const core::SourceLoc &loc) {
    if (source.producer_node_id == 0 || source.producer_port_id == 0) return;
    if (target_node_id == 0 || target_port_id == 0) return;

    // Avoid self-loops
    if (source.producer_node_id == target_node_id) return;

    TopologyEdge edge;
    edge.source_node_id = source.producer_node_id;
    edge.source_port_id = source.producer_port_id;
    edge.target_node_id = target_node_id;
    edge.target_port_id = target_port_id;
    edge.status = TopologyEdge::Status::kUnknown;
    edge.loc = loc;
    edge.context_node_id = current_context_id_;

    graph_.AddEdge(std::move(edge));
}

} // namespace polyglot::tools::topo
