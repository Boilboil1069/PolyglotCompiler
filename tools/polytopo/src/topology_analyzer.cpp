/**
 * @file     topology_analyzer.cpp
 * @brief    Builds a TopologyGraph from a .poly AST
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cassert>

#include "tools/polytopo/include/topology_analyzer.h"

namespace polyglot::tools::topo {

// ============================================================================
// Construction
// ============================================================================

TopologyAnalyzer::TopologyAnalyzer(const ploy::PloySema &sema) : sema_(sema) {}

// ============================================================================
// Public API
// ============================================================================

bool TopologyAnalyzer::Build(const std::shared_ptr<ploy::Module> &module) {
  if (!module)
    return false;

  graph_ = TopologyGraph{};
  var_bindings_.clear();
  external_nodes_.clear();
  return_boundaries_.clear();
  call_instances_.clear();
  graph_.module_name = module->filename;
  graph_.source_file = module->filename;

  // First pass: register all top-level declarations as nodes
  for (const auto &stmt : module->declarations) {
    AnalyzeStatement(stmt);
  }

  auto analyze = [&](const std::string &name, const auto &body) {
    const auto *node = graph_.FindNodeByName(name);
    if (!node) return;
    const auto id = node->id;
    var_bindings_.clear();
    current_context_id_ = id;
    BindBoundary(id);
    AnalyzeBody(body, id);
    current_context_id_ = 0;
  };
  for (const auto &stmt : module->declarations) {
    if (auto func = std::dynamic_pointer_cast<ploy::FuncDecl>(stmt)) {
      analyze(func->name, func->body);
    } else if (auto pipeline = std::dynamic_pointer_cast<ploy::PipelineDecl>(stmt)) {
      analyze("pipeline:" + pipeline->name, pipeline->body);
      for (const auto &body_stmt : pipeline->body) {
        if (auto stage = std::dynamic_pointer_cast<ploy::FuncDecl>(body_stmt))
          analyze("pipeline:" + pipeline->name + "::" + stage->name, stage->body);
      }
    } else if (auto map = std::dynamic_pointer_cast<ploy::MapFuncDecl>(stmt)) {
      analyze("map:" + map->name, map->body);
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
  node.language = "poly";
  node.kind = TopologyNode::Kind::kFunction;
  node.loc = func->loc;

  // Input ports from parameters
  for (size_t i = 0; i < func->params.size(); ++i) {
    Port port;
    port.name = func->params[i].name;
    port.direction = Port::Direction::kInput;
    port.type = ResolveType(func->params[i].type);
    port.language = "poly";
    port.index = static_cast<int>(i);
    node.inputs.push_back(std::move(port));
  }

  // Output port from return type
  Port ret_port;
  ret_port.name = "return";
  ret_port.direction = Port::Direction::kOutput;
  ret_port.type = func->return_type ? ResolveType(func->return_type) : core::Type::Void();
  ret_port.language = "poly";
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
    const ploy::FunctionSignature *sig =
        (sig_it != sema_.KnownSignatures().end()) ? &sig_it->second : nullptr;
    if (sig && !sig->param_types.empty()) {
      for (size_t i = 0; i < sig->param_types.size(); ++i) {
        Port port;
        port.name = i < sig->param_names.size() ? sig->param_names[i] : "arg" + std::to_string(i);
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
    const ploy::FunctionSignature *sig =
        (src_sig_it != sema_.KnownSignatures().end()) ? &src_sig_it->second : nullptr;
    if (sig && !sig->param_types.empty()) {
      for (size_t i = 0; i < sig->param_types.size(); ++i) {
        Port port;
        port.name = i < sig->param_names.size() ? sig->param_names[i] : "arg" + std::to_string(i);
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
    edge.relation = "binding";
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
  node.language = "poly";
  node.kind = TopologyNode::Kind::kPipeline;
  node.loc = pipeline->loc;

  // Pipeline has a single output (the result of the pipeline)
  Port out;
  out.name = "result";
  out.direction = Port::Direction::kOutput;
  out.type = core::Type::Any();
  out.language = "poly";
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
    if (!func)
      continue;

    TopologyNode stage;
    stage.name = "pipeline:" + pipeline->name + "::" + func->name;
    stage.language = "poly";
    stage.kind = TopologyNode::Kind::kFunction;
    stage.loc = func->loc;
    stage.origin = TopologyNode::Origin::kPipelineStage; // visible in PIPELINE view

    // Input ports from parameters
    for (size_t i = 0; i < func->params.size(); ++i) {
      Port port;
      port.name = func->params[i].name;
      port.direction = Port::Direction::kInput;
      port.type = ResolveType(func->params[i].type);
      port.language = "poly";
      port.index = static_cast<int>(i);
      stage.inputs.push_back(std::move(port));
    }

    // Output port from return type
    Port ret_port;
    ret_port.name = "return";
    ret_port.direction = Port::Direction::kOutput;
    ret_port.type = func->return_type ? ResolveType(func->return_type) : core::Type::Void();
    ret_port.language = "poly";
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
    if (!src || !tgt || src->outputs.empty() || tgt->inputs.empty())
      continue;

    TopologyEdge edge;
    edge.source_node_id = src->id;
    edge.source_port_id = src->outputs[0].id;
    edge.target_node_id = tgt->id;
    edge.target_port_id = tgt->inputs[0].id;
    edge.origin = TopologyEdge::Origin::kPipelineStage;
    edge.status = TopologyEdge::Status::kValid;
    edge.loc = tgt->loc;
    edge.conversion_note = "Declared stage order; no value transfer inferred";
    edge.relation = "order";
    graph_.AddEdge(std::move(edge));
  }
}

void TopologyAnalyzer::AnalyzeMapFuncDecl(const std::shared_ptr<ploy::MapFuncDecl> &map_func) {
  TopologyNode node;
  node.name = "map:" + map_func->name;
  node.language = "poly";
  node.kind = TopologyNode::Kind::kMapFunc;
  node.loc = map_func->loc;

  for (size_t i = 0; i < map_func->params.size(); ++i) {
    Port port;
    port.name = map_func->params[i].name;
    port.direction = Port::Direction::kInput;
    port.type = ResolveType(map_func->params[i].type);
    port.language = "poly";
    port.index = static_cast<int>(i);
    node.inputs.push_back(std::move(port));
  }

  Port ret;
  ret.name = "return";
  ret.direction = Port::Direction::kOutput;
  ret.type = map_func->return_type ? ResolveType(map_func->return_type) : core::Type::Any();
  ret.language = "poly";
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
      ret.type = method->return_type ? ResolveType(method->return_type) : core::Type::Void();
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

void TopologyAnalyzer::AnalyzeBody(const std::vector<std::shared_ptr<ploy::Statement>> &stmts,
                                   uint64_t context_node_id) {
  for (const auto &stmt : stmts) {
    AnalyzeBodyStatement(stmt, context_node_id);
  }
}

void TopologyAnalyzer::AnalyzeBodyStatement(const std::shared_ptr<ploy::Statement> &stmt,
                                            uint64_t context_node_id) {
  if (auto var = std::dynamic_pointer_cast<ploy::VarDecl>(stmt)) {
    // Variable declaration: track which node/port produced the value
    if (var->init) {
      auto result = AnalyzeExpression(var->init, context_node_id);
      var_bindings_[var->name] = {result.producer_node_id, result.producer_port_id, result.type};
    }
  } else if (auto expr_stmt = std::dynamic_pointer_cast<ploy::ExprStatement>(stmt)) {
    if (expr_stmt->expr) {
      AnalyzeExpression(expr_stmt->expr, context_node_id);
    }
  } else if (auto ret = std::dynamic_pointer_cast<ploy::ReturnStatement>(stmt)) {
    if (ret->value) {
      auto result = AnalyzeExpression(ret->value, context_node_id);
      // Connect the return value to the context node's output port
      auto boundary = return_boundaries_.find(context_node_id);
      if (boundary != return_boundaries_.end()) {
        const auto *target = graph_.GetNode(boundary->second);
        if (target && !target->inputs.empty())
          ConnectEdge(result, target->id, target->inputs[0].id, ret->loc);
      }
    }
  } else if (auto if_stmt = std::dynamic_pointer_cast<ploy::IfStatement>(stmt)) {
    const auto condition = AnalyzeExpression(if_stmt->condition, context_node_id);
    const auto before = var_bindings_;
    AnalyzeBody(if_stmt->then_body, context_node_id);
    const auto then_values = var_bindings_;
    var_bindings_ = before;
    AnalyzeBody(if_stmt->else_body, context_node_id);
    const auto else_values = var_bindings_;
    // Keep only names visible before the branch. A branch-local declaration
    // cannot leak out, and differing values need an explicit alternatives node.
    var_bindings_ = before;
    for (const auto &[name, original] : before) {
      auto left = then_values.find(name), right = else_values.find(name);
      if (left == then_values.end() || right == else_values.end()) continue;
      if (left->second.producer_port_id == right->second.producer_port_id) {
        var_bindings_[name] = left->second;
        continue;
      }
      const auto &a = left->second; const auto &b = right->second;
      auto type = a.type == b.type ? a.type : core::Type::Any();
      auto result = MakeExpressionNode("IF alternatives: " + name, TopologyNode::Kind::kOperation,
          {{a.producer_node_id, a.producer_port_id, a.type},
           {b.producer_node_id, b.producer_port_id, b.type}, condition}, type, if_stmt->loc);
      var_bindings_[name] = {result.producer_node_id, result.producer_port_id, result.type};
    }
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
      var_bindings_[with_stmt->var_name] = {result.producer_node_id, result.producer_port_id,
                                            result.type};
    }
    AnalyzeBody(with_stmt->body, context_node_id);
  }
}

// ============================================================================
// Expression analysis (data-flow edge extraction)
// ============================================================================

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeExpression(
    const std::shared_ptr<ploy::Expression> &expr, uint64_t context_node_id) {
  if (!expr)
    return {};

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
      return {it->second.producer_node_id, it->second.producer_port_id, it->second.type, ident->name};
    }
    return MakeExpressionNode("unresolved: " + ident->name, TopologyNode::Kind::kValue, {},
                              core::Type::Any(), ident->loc);
  } else if (auto literal = std::dynamic_pointer_cast<ploy::Literal>(expr)) {
    core::Type type = core::Type::Any();
    switch (literal->kind) {
    case ploy::Literal::Kind::kInteger: type = core::Type::Int(64, true); break;
    case ploy::Literal::Kind::kFloat: type = core::Type::Float(64); break;
    case ploy::Literal::Kind::kString: type = core::Type::String(); break;
    case ploy::Literal::Kind::kBool: type = core::Type::Bool(); break;
    case ploy::Literal::Kind::kNull: break;
    }
    return MakeExpressionNode(literal->value, TopologyNode::Kind::kValue, {}, type, literal->loc);
  } else if (auto binary = std::dynamic_pointer_cast<ploy::BinaryExpression>(expr)) {
    if (binary->op == "=") {
      auto result = AnalyzeExpression(binary->right, context_node_id);
      if (auto name = std::dynamic_pointer_cast<ploy::Identifier>(binary->left)) {
        var_bindings_[name->name] = {result.producer_node_id, result.producer_port_id, result.type};
        result.value_name = name->name;
      }
      return result;
    }
    auto left = AnalyzeExpression(binary->left, context_node_id);
    auto right = AnalyzeExpression(binary->right, context_node_id);
    const auto &op = binary->op;
    auto type = core::Type::Any();
    const bool comparison = op == "==" || op == "!=" || op == "<" || op == ">" ||
                            op == "<=" || op == ">=" || op == "AND" || op == "OR";
    if (comparison) type = core::Type::Bool();
    else if (left.type.kind == right.type.kind) type = left.type;
    else if ((left.type.kind == core::TypeKind::kInt && right.type.kind == core::TypeKind::kFloat) ||
             (left.type.kind == core::TypeKind::kFloat && right.type.kind == core::TypeKind::kInt))
      type = core::Type::Float(64);
    return MakeExpressionNode(op, TopologyNode::Kind::kOperation, {left, right}, type, binary->loc);
  } else if (auto unary = std::dynamic_pointer_cast<ploy::UnaryExpression>(expr)) {
    auto operand = AnalyzeExpression(unary->operand, context_node_id);
    auto type = unary->op == "NOT" || unary->op == "!" ? core::Type::Bool() : operand.type;
    return MakeExpressionNode(unary->op, TopologyNode::Kind::kOperation, {operand}, type, unary->loc);
  } else if (auto convert = std::dynamic_pointer_cast<ploy::ConvertExpression>(expr)) {
    auto operand = AnalyzeExpression(convert->expr, context_node_id);
    return MakeExpressionNode("CONVERT", TopologyNode::Kind::kConversion, {operand},
                              ResolveType(convert->target_type), convert->loc);
  } else if (auto named = std::dynamic_pointer_cast<ploy::NamedArgument>(expr)) {
    return AnalyzeExpression(named->value, context_node_id);
  }
  return MakeExpressionNode("unresolved expression", TopologyNode::Kind::kValue, {},
                            core::Type::Any(), expr->loc);
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeCrossLangCall(
    const std::shared_ptr<ploy::CrossLangCallExpression> &call, uint64_t context_node_id) {
  auto id = FindOrCreateExternalNode(call->language, call->function, call->loc);
  // Each call is an instance; sharing a target would merge unrelated values.
  id = CreateCallInstance(id, call->loc);
  auto *instance = graph_.GetMutableNode(id);
  if (instance->description == "Signature unresolved") {
    instance->inputs.clear();
    for (size_t i = 0; i < call->args.size(); ++i) {
      Port port;
      port.id = graph_.AllocPortId();
      port.name = "argument " + std::to_string(i + 1) + " ?";
      port.type = core::Type::Any();
      port.language = call->language;
      port.index = static_cast<int>(i);
      instance->inputs.push_back(port);
    }
  }
  auto inputs = graph_.GetNode(id)->inputs;
  for (size_t i = 0; i < call->args.size(); ++i) {
    auto result = AnalyzeExpression(call->args[i], context_node_id);
    size_t slot = i;
    if (auto named = std::dynamic_pointer_cast<ploy::NamedArgument>(call->args[i])) {
      slot = inputs.size();
      for (size_t j = 0; j < inputs.size(); ++j)
        if (inputs[j].name == named->name) slot = j;
    }
    if (slot < inputs.size()) ConnectEdge(result, id, inputs[slot].id, call->loc);
  }
  const auto *node = graph_.GetNode(id);
  if (!node->outputs.empty()) return {id, node->outputs[0].id, node->outputs[0].type};
  return {id, 0, core::Type::Any()};
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeNewExpression(
    const std::shared_ptr<ploy::NewExpression> &expr, uint64_t context_node_id) {
  auto call = std::make_shared<ploy::CrossLangCallExpression>();
  call->language = expr->language;
  call->function = expr->class_name + "::new";
  call->args = expr->args;
  call->loc = expr->loc;
  auto result = AnalyzeCrossLangCall(call, context_node_id);
  auto *node = graph_.GetMutableNode(result.producer_node_id);
  node->kind = TopologyNode::Kind::kConstructor;
  result.type = core::Type{core::TypeKind::kClass, expr->class_name};
  if (!node->outputs.empty()) node->outputs[0].type = result.type;
  return result;
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeMethodCall(
    const std::shared_ptr<ploy::MethodCallExpression> &expr, uint64_t context_node_id) {
  auto call = std::make_shared<ploy::CrossLangCallExpression>();
  call->language = expr->language;
  call->function = "method::" + expr->method_name;
  call->args = {expr->object};
  call->args.insert(call->args.end(), expr->args.begin(), expr->args.end());
  call->loc = expr->loc;
  auto result = AnalyzeCrossLangCall(call, context_node_id);
  graph_.GetMutableNode(result.producer_node_id)->kind = TopologyNode::Kind::kMethod;
  return result;
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::AnalyzeCallExpression(
    const std::shared_ptr<ploy::CallExpression> &call, uint64_t context_node_id) {
  std::string name;
  if (auto ident = std::dynamic_pointer_cast<ploy::Identifier>(call->callee)) name = ident->name;
  if (auto ident = std::dynamic_pointer_cast<ploy::QualifiedIdentifier>(call->callee))
    name = ident->qualifier + "::" + ident->name;
  const auto *prototype = graph_.FindNodeByName(name);
  if (!prototype) {
    auto external = std::make_shared<ploy::CrossLangCallExpression>();
    external->language = "poly";
    external->function = name.empty() ? "unresolved call" : name;
    external->args = call->args;
    external->loc = call->loc;
    return AnalyzeCrossLangCall(external, context_node_id);
  }
  auto id = CreateCallInstance(prototype->id, call->loc);
  auto inputs = graph_.GetNode(id)->inputs;
  for (size_t i = 0; i < call->args.size(); ++i) {
    auto result = AnalyzeExpression(call->args[i], context_node_id);
    size_t slot = i;
    if (auto named = std::dynamic_pointer_cast<ploy::NamedArgument>(call->args[i])) {
      slot = inputs.size();
      for (size_t j = 0; j < inputs.size(); ++j)
        if (inputs[j].name == named->name) slot = j;
    }
    if (slot < inputs.size()) ConnectEdge(result, id, inputs[slot].id, call->loc);
  }
  const auto *node = graph_.GetNode(id);
  if (!node->outputs.empty()) return {id, node->outputs[0].id, node->outputs[0].type};
  return {id, 0, core::Type::Any()};
}

void TopologyAnalyzer::BindBoundary(uint64_t context_node_id) {
  const auto context = *graph_.GetNode(context_node_id);
  if (!context.inputs.empty()) {
    TopologyNode input;
    input.name = context.name + "::inputs";
    input.display_name = context.name + " · inputs";
    input.kind = TopologyNode::Kind::kBoundary;
    input.language = "poly";
    input.origin = TopologyNode::Origin::kCall;
    input.context_node_id = context_node_id;
    input.loc = context.loc;
    input.outputs = context.inputs;
    for (auto &p : input.outputs) p.direction = Port::Direction::kOutput;
    const auto id = graph_.AddNode(std::move(input));
    for (const auto &p : graph_.GetNode(id)->outputs) var_bindings_[p.name] = {id, p.id, p.type};
  }
  if (!context.outputs.empty() && context.outputs[0].type.kind != core::TypeKind::kVoid) {
    TopologyNode output;
    output.name = context.name + "::result";
    output.display_name = context.name + " · result";
    output.kind = TopologyNode::Kind::kBoundary;
    output.language = "poly";
    output.origin = TopologyNode::Origin::kCall;
    output.context_node_id = context_node_id;
    output.loc = context.loc;
    output.inputs = context.outputs;
    for (auto &p : output.inputs) p.direction = Port::Direction::kInput;
    return_boundaries_[context_node_id] = graph_.AddNode(std::move(output));
  }
}

uint64_t TopologyAnalyzer::CreateCallInstance(uint64_t prototype, const core::SourceLoc &loc) {
  auto node = *graph_.GetNode(prototype);
  // The first external node is already a call instance; LINK and declaration
  // nodes remain separate so their binding edges cannot be mistaken for values.
  auto &count = call_instances_[node.name];
  if (node.origin == TopologyNode::Origin::kCall && count++ == 0) return prototype;
  node.display_name = node.display_name.empty() ? node.name : node.display_name;
  node.name += "@" + std::to_string(loc.line) + ":" + std::to_string(++count);
  if (node.definition_loc.file.empty() && node.origin != TopologyNode::Origin::kCall)
    node.definition_loc = node.loc;
  node.loc = loc;
  node.origin = TopologyNode::Origin::kCall;
  node.context_node_id = current_context_id_;
  if (node.description != "Signature unresolved")
    node.description = "Call instance; inputs are argument values and output is its result";
  return graph_.AddNode(std::move(node));
}

TopologyAnalyzer::ExprResult TopologyAnalyzer::MakeExpressionNode(
    const std::string &label, TopologyNode::Kind kind, const std::vector<ExprResult> &args,
    const core::Type &type, const core::SourceLoc &loc) {
  TopologyNode node;
  node.name = "expr:" + std::to_string(graph_.NodeCount()) + ":" + label;
  node.display_name = label;
  node.language = "poly";
  node.kind = kind;
  node.origin = TopologyNode::Origin::kCall;
  node.context_node_id = current_context_id_;
  node.loc = loc;
  for (size_t i = 0; i < args.size(); ++i) {
    Port p;
    p.name = args.size() == 1 ? "value" : (i == 0 ? "left" : (i == 1 ? "right" : "condition"));
    p.type = args[i].type;
    p.language = "poly";
    p.index = static_cast<int>(i);
    node.inputs.push_back(p);
  }
  Port out;
  out.name = "value";
  out.direction = Port::Direction::kOutput;
  out.type = type;
  out.language = "poly";
  node.outputs.push_back(out);
  const auto id = graph_.AddNode(std::move(node));
  for (size_t i = 0; i < args.size(); ++i)
    ConnectEdge(args[i], id, graph_.GetNode(id)->inputs[i].id, loc);
  return {id, graph_.GetNode(id)->outputs[0].id, type};
}

// ============================================================================
// Helpers
// ============================================================================

core::Type TopologyAnalyzer::ResolveType(const std::shared_ptr<ploy::TypeNode> &type_node) const {
  if (!type_node)
    return core::Type::Any();

  if (auto simple = std::dynamic_pointer_cast<ploy::SimpleType>(type_node)) {
    if (simple->name == "INT" || simple->name == "int")
      return core::Type::Int(64, true);
    if (simple->name == "FLOAT" || simple->name == "float")
      return core::Type::Float(64);
    if (simple->name == "BOOL" || simple->name == "bool")
      return core::Type::Bool();
    if (simple->name == "STRING" || simple->name == "string")
      return core::Type::String();
    if (simple->name == "VOID" || simple->name == "void")
      return core::Type::Void();
    if (simple->name == "i32" || simple->name == "i64" || simple->name == "INT32" || simple->name == "INT64")
      return core::Type::Int(simple->name == "i32" || simple->name == "INT32" ? 32 : 64, true);
    if (simple->name == "f32" || simple->name == "f64" || simple->name == "FLOAT32" || simple->name == "FLOAT64")
      return core::Type::Float(simple->name == "f32" || simple->name == "FLOAT32" ? 32 : 64);
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

uint64_t TopologyAnalyzer::FindOrCreateExternalNode(const std::string &language,
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
    node.definition_loc = sig->defined_at;
    for (size_t i = 0; i < sig->param_types.size(); ++i) {
      Port port;
      port.name = i < sig->param_names.size() ? sig->param_names[i] : "arg" + std::to_string(i);
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
    node.description = "Signature unresolved";
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

void TopologyAnalyzer::ConnectEdge(const ExprResult &source, uint64_t target_node_id,
                                   uint64_t target_port_id, const core::SourceLoc &loc) {
  if (source.producer_node_id == 0 || source.producer_port_id == 0)
    return;
  if (target_node_id == 0 || target_port_id == 0)
    return;

  // Avoid self-loops
  if (source.producer_node_id == target_node_id)
    return;

  TopologyEdge edge;
  edge.source_node_id = source.producer_node_id;
  edge.source_port_id = source.producer_port_id;
  edge.target_node_id = target_node_id;
  edge.target_port_id = target_port_id;
  edge.status = TopologyEdge::Status::kUnknown;
  edge.value_label = source.value_name;
  edge.loc = loc;
  edge.context_node_id = current_context_id_;

  graph_.AddEdge(std::move(edge));
}

} // namespace polyglot::tools::topo
