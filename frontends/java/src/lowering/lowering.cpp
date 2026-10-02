#include "frontends/common/include/native_builtins.h"
#include "frontends/common/include/native_string_literal.h"
/**
 * @file     lowering.cpp
 * @brief    Java language frontend implementation
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include "middle/include/ir/ir_builder.h"

#include "frontends/java/include/java_lowering.h"

namespace polyglot::java {
namespace {

using Name = std::string;

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &node, frontends::Diagnostics &diags,
                    const core::SourceLoc &fallback_loc) {
  if (!node) {
    diags.ReportError(fallback_loc, frontends::ErrorCode::kUnsupportedLowering,
                      "Java lowering requires a resolved runtime type");
    return ir::IRType::Invalid();
  }
  if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
    auto &n = simple->name;
    if (n == "byte")
      return ir::IRType::I8(true);
    if (n == "short")
      return ir::IRType::I16(true);
    if (n == "int")
      return ir::IRType::I32(true);
    if (n == "long")
      return ir::IRType::I64(true);
    if (n == "float")
      return ir::IRType::F32();
    if (n == "double")
      return ir::IRType::F64();
    if (n == "boolean")
      return ir::IRType::I1();
    if (n == "char")
      return ir::IRType::I16(false);
    if (n == "void")
      return ir::IRType::Void();
    if (n == "String" || n == "java.lang.String")
      return ir::IRType::Pointer(ir::IRType::I8());
    return ir::IRType::Pointer(ir::IRType::I8()); // reference types
  }
  if (auto arr = std::dynamic_pointer_cast<ArrayType>(node)) {
    if (arr->dimensions != 1) {
      diags.ReportError(arr->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "multidimensional Java array layout is not supported");
      return ir::IRType::Invalid();
    }
    auto element = ToIRType(arr->element_type, diags, arr->loc);
    return element.kind == ir::IRTypeKind::kInvalid ? element : ir::IRType::Pointer(element);
  }
  diags.ReportError(node->loc, frontends::ErrorCode::kUnsupportedLowering,
                    "unsupported Java runtime type reached IR lowering");
  return ir::IRType::Invalid();
}

struct EnvEntry {
  Name value;
  ir::IRType type{ir::IRType::Invalid()};
};

struct LoweringContext {
  ir::IRContext &ir_ctx;
  frontends::Diagnostics &diags;
  std::unordered_map<Name, EnvEntry> env;
  std::unordered_map<Name, Name> local_addresses;
  std::string current_class;
  struct StaticSignature {
    ir::IRType result;
    std::vector<ir::IRType> params;
    bool ambiguous{false};
  };
  std::unordered_map<std::string, StaticSignature> static_methods;
  ir::IRBuilder builder;
  std::shared_ptr<ir::Function> fn;
  bool terminated{false};

  LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d) :
      ir_ctx(ctx), diags(d), builder(ctx) {}
};

struct EvalResult {
  Name value;
  ir::IRType type{ir::IRType::Invalid()};
};

bool IsIntegerLiteral(const std::string &text, long long *out) {
  char *end = nullptr;
  long long v = std::strtoll(text.c_str(), &end, 0);
  if (end == text.c_str() || *end != '\0')
    return false;
  if (out)
    *out = v;
  return true;
}

bool IsFloatLiteral(const std::string &text, double *out) {
  char *end = nullptr;
  double v = std::strtod(text.c_str(), &end);
  if (end == text.c_str() ||
      (*end != '\0' && *end != 'f' && *end != 'F' && *end != 'd' && *end != 'D'))
    return false;
  if (out)
    *out = v;
  return true;
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc);
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);

EvalResult MakeLiteral(long long v, LoweringContext &) {
  return {std::to_string(v), ir::IRType::I64(true)};
}

EvalResult MakeFloatLiteral(double v, LoweringContext &lc) {
  auto lit = lc.builder.MakeLiteral(v);
  return {lit->name, ir::IRType::F64()};
}

bool FlattenMemberName(const std::shared_ptr<Expression> &expr, std::string &out) {
  if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
    out = id->name;
    return true;
  }
  if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
    std::string base;
    if (!FlattenMemberName(member->object, base))
      return false;
    out = base + "." + member->member;
    return true;
  }
  return false;
}

// Map binary operator string to BinaryInstruction::Op
bool MapBinOp(const std::string &op, bool is_float,
              ir::BinaryInstruction::Op &mapped) {
  if (op == "+")
    mapped = is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
  if (op == "-")
    mapped = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
  if (op == "*")
    mapped = is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
  if (op == "/")
    mapped = is_float ? ir::BinaryInstruction::Op::kFDiv : ir::BinaryInstruction::Op::kSDiv;
  if (op == "%")
    mapped = is_float ? ir::BinaryInstruction::Op::kFRem : ir::BinaryInstruction::Op::kSRem;
  if (op == "&")
    mapped = ir::BinaryInstruction::Op::kAnd;
  if (op == "|")
    mapped = ir::BinaryInstruction::Op::kOr;
  if (op == "^")
    mapped = ir::BinaryInstruction::Op::kXor;
  if (op == "<<")
    mapped = ir::BinaryInstruction::Op::kShl;
  if (op == ">>")
    mapped = ir::BinaryInstruction::Op::kAShr;
  if (op == ">>>")
    mapped = ir::BinaryInstruction::Op::kLShr;
  if (op == "==")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFoe
                      : ir::BinaryInstruction::Op::kCmpEq;
  if (op == "!=")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFne
                      : ir::BinaryInstruction::Op::kCmpNe;
  if (op == "<")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFlt
                      : ir::BinaryInstruction::Op::kCmpSlt;
  if (op == "<=")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFle
                      : ir::BinaryInstruction::Op::kCmpSle;
  if (op == ">")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFgt
                      : ir::BinaryInstruction::Op::kCmpSgt;
  if (op == ">=")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFge
                      : ir::BinaryInstruction::Op::kCmpSge;
  return op == "+" || op == "-" || op == "*" || op == "/" || op == "%" || op == "&" ||
         op == "|" || op == "^" || op == "<<" || op == ">>" || op == ">>>" || op == "==" ||
         op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

bool IsCmpOp(ir::BinaryInstruction::Op op) {
  switch (op) {
  case ir::BinaryInstruction::Op::kCmpEq:
  case ir::BinaryInstruction::Op::kCmpNe:
  case ir::BinaryInstruction::Op::kCmpSlt:
  case ir::BinaryInstruction::Op::kCmpSle:
  case ir::BinaryInstruction::Op::kCmpSgt:
  case ir::BinaryInstruction::Op::kCmpSge:
  case ir::BinaryInstruction::Op::kCmpFoe:
  case ir::BinaryInstruction::Op::kCmpFne:
  case ir::BinaryInstruction::Op::kCmpFlt:
  case ir::BinaryInstruction::Op::kCmpFle:
  case ir::BinaryInstruction::Op::kCmpFgt:
  case ir::BinaryInstruction::Op::kCmpFge:
    return true;
  default:
    return false;
  }
}

/** @name Expression evaluation */
/** @{ */

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
  if (!expr)
    return {};

  if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
    auto it = lc.env.find(id->name);
    if (it != lc.env.end() && !it->second.value.empty()) {
      auto address = lc.local_addresses.find(id->name);
      if (address != lc.local_addresses.end()) {
        auto load = lc.builder.MakeLoad(address->second, it->second.type);
        return {load->name, load->type};
      }
      return {it->second.value, it->second.type};
    }
    if (it != lc.env.end()) {
      lc.diags.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "use of an uninitialized Java local in IR lowering: " + id->name);
    } else {
      lc.diags.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unresolved Java value in IR lowering: " + id->name);
    }
    return {};
  }

  if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
    auto &v = lit->value;
    if (v == "true")
      return {"1", ir::IRType::I1()};
    if (v == "false")
      return {"0", ir::IRType::I1()};
    if (v == "null")
      return {"0", ir::IRType::Pointer(ir::IRType::I8())};

    if (v.starts_with("\"\"\"")) {
      lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Java text-block normalization is not implemented in IR lowering");
      return {};
    }

    if (!v.empty() && v[0] == '"') {
      const bool raw = v.starts_with("@\"");
      std::string body = v.substr(raw ? 2 : 1, v.size() - (raw ? 3 : 2));
      if (raw) {
        for (std::size_t i = 0; (i = body.find("\"\"", i)) != std::string::npos; ++i)
          body.erase(i, 1);
      }
      std::string bytes, error;
      if (!frontends::DecodeNativeString(body, raw, false, bytes, error)) {
        lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering, error);
        return {};
      }
      auto name = lc.builder.MakeStringLiteral(bytes, "jstr");
      return {name, ir::IRType::Pointer(ir::IRType::I8())};
    }

    long long iv;
    if (IsIntegerLiteral(v, &iv))
      return MakeLiteral(iv, lc);

    double fv;
    if (IsFloatLiteral(v, &fv))
      return MakeFloatLiteral(fv, lc);

    if (!v.empty() && v[0] == '\'') {
      if (v.size() == 3)
        return MakeLiteral(static_cast<long long>(v[1]), lc);
      lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "escaped Java character literal lowering is not implemented");
      return {};
    }

    lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported Java literal reached IR lowering: " + v);
    return {};
  }

  if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    auto operand = EvalExpr(unary->operand, lc);
    if (operand.type.kind == ir::IRTypeKind::kInvalid)
      return {};
    if (unary->op == "-") {
      const bool fp = operand.type.kind == ir::IRTypeKind::kF32 ||
                      operand.type.kind == ir::IRTypeKind::kF64;
      if (fp) {
        auto neg = lc.builder.MakeFloatNegate(operand.value, operand.type, "");
        return {neg->name, operand.type};
      }
      auto neg = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kSub,
                                       "0", operand.value, "");
      neg->type = operand.type;
      return {neg->name, operand.type};
    }
    if (unary->op == "!") {
      auto not_val = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor, operand.value, "1", "");
      not_val->type = ir::IRType::I1();
      return {not_val->name, ir::IRType::I1()};
    }
    if (unary->op == "~") {
      auto comp = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor, operand.value, "-1", "");
      comp->type = operand.type;
      return {comp->name, operand.type};
    }
    if (unary->op == "+")
      return operand;
    lc.diags.ReportError(unary->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported Java unary operator in lowering: " + unary->op);
    return {};
  }

  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    // Assignment
    if (bin->op == "=") {
      auto rhs = EvalExpr(bin->right, lc);
      if (rhs.type.kind == ir::IRTypeKind::kInvalid)
        return {};
      if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
        if (!lc.env.contains(id->name)) {
          lc.diags.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "Java field or unresolved assignment target requires storage lowering: " +
                                   id->name);
          return {};
        }
        if (lc.local_addresses.count(id->name))
          lc.builder.MakeStore(lc.local_addresses.at(id->name), rhs.value);
        else lc.env[id->name] = {rhs.value, rhs.type};
      } else {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Java lowering only supports identifier assignment targets");
        return {};
      }
      return rhs;
    }

    // Compound assignment
    if (bin->op == "+=" || bin->op == "-=" || bin->op == "*=" || bin->op == "/=" ||
        bin->op == "%=") {
      auto left = EvalExpr(bin->left, lc);
      auto right = EvalExpr(bin->right, lc);
      if (left.type.kind == ir::IRTypeKind::kInvalid ||
          right.type.kind == ir::IRTypeKind::kInvalid)
        return {};
      std::string base_op = bin->op.substr(0, bin->op.size() - 1);
      bool is_float =
          (left.type.kind == ir::IRTypeKind::kF32 || left.type.kind == ir::IRTypeKind::kF64);
      ir::BinaryInstruction::Op op;
      if (!MapBinOp(base_op, is_float, op)) {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "unsupported Java binary operator: " + base_op);
        return {};
      }
      auto result = lc.builder.MakeBinary(op, left.value, right.value, "");
      result->type = left.type;
      if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
        if (lc.local_addresses.count(id->name))
          lc.builder.MakeStore(lc.local_addresses.at(id->name), result->name);
        else lc.env[id->name] = {result->name, left.type};
      } else {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Java lowering only supports identifier compound-assignment targets");
        return {};
      }
      return {result->name, left.type};
    }
    if (bin->op == "&=" || bin->op == "|=" || bin->op == "^=" || bin->op == "<<=" ||
        bin->op == ">>=" || bin->op == ">>>=") {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported Java compound assignment operator: " + bin->op);
      return {};
    }

    // Logical short-circuit operators
    if (bin->op == "&&" || bin->op == "||") {
      auto left = EvalExpr(bin->left, lc);
      if (left.type.kind != ir::IRTypeKind::kI1) {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Java logical operands must be boolean");
        return {};
      }
      auto *left_block = lc.builder.GetInsertPoint().get();
      auto rhs_block = lc.builder.CreateBlock("logical.rhs");
      auto merge_block = lc.builder.CreateBlock("logical.end");
      if (bin->op == "&&")
        lc.builder.MakeCondBranch(left.value, rhs_block.get(), merge_block.get());
      else
        lc.builder.MakeCondBranch(left.value, merge_block.get(), rhs_block.get());
      lc.builder.SetInsertPoint(rhs_block);
      auto right = EvalExpr(bin->right, lc);
      if (right.type.kind != ir::IRTypeKind::kI1) {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Java logical operands must be boolean");
        return {};
      }
      auto *right_end = lc.builder.GetInsertPoint().get();
      lc.builder.MakeBranch(merge_block.get());
      lc.builder.SetInsertPoint(merge_block);
      auto phi = lc.builder.MakePhi(ir::IRType::I1(),
          {{left_block, left.value}, {right_end, right.value}}, "logical.value");
      return {phi->name, ir::IRType::I1()};
    }

    auto left = EvalExpr(bin->left, lc);
    auto right = EvalExpr(bin->right, lc);
    if (left.type.kind == ir::IRTypeKind::kInvalid || right.type.kind == ir::IRTypeKind::kInvalid)
      return {};

    bool is_float =
        (left.type.kind == ir::IRTypeKind::kF32 || left.type.kind == ir::IRTypeKind::kF64);
    if (left.type.kind == ir::IRTypeKind::kPointer && bin->op != "==" && bin->op != "!=") {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Java reference/string operators require JVM runtime semantics");
      return {};
    }
    ir::BinaryInstruction::Op op;
    if (!MapBinOp(bin->op, is_float, op)) {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported Java binary operator: " + bin->op);
      return {};
    }
    auto inst = lc.builder.MakeBinary(op, left.value, right.value, "");
    ir::IRType result_type = IsCmpOp(op) ? ir::IRType::I1() : left.type;
    inst->type = result_type;
    return {inst->name, result_type};
  }

  if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
    std::vector<std::string> arg_values;
    std::vector<ir::IRType> arg_types;
    for (auto &arg : call->args) {
      auto r = EvalExpr(arg, lc);
      if (r.type.kind == ir::IRTypeKind::kInvalid)
        return {};
      arg_values.push_back(r.value);
      arg_types.push_back(r.type);
    }

    std::string callee_name;
    if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
      callee_name = id->name;
    } else if (!FlattenMemberName(call->callee, callee_name)) {
      lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "indirect Java calls are not supported by lowering");
      return {};
    }

    // Map well-known Java methods
    if (callee_name == "System.out.println" || callee_name == "System.out.print") {
      auto inst = lc.builder.MakeCall("__ploy_java_print", arg_values, ir::IRType::Void(), "");
      return {inst->name, inst->type};
    }

    if (const auto *api = frontends::FindNativeBuiltin(callee_name)) {
      auto inst = frontends::EmitNativeBuiltin(*api, arg_values, arg_types, lc.builder, lc.ir_ctx, lc.diags, call->loc);
      if (!inst) return {};
      return {inst->name, inst->type};
    }
    std::string resolved = callee_name;
    const auto dot = resolved.rfind('.');
    if (dot != std::string::npos) resolved.replace(dot, 1, "::");
    else if (!lc.current_class.empty()) resolved = lc.current_class + "::" + resolved;
    const auto signature = lc.static_methods.find(resolved);
    if (signature != lc.static_methods.end() && !signature->second.ambiguous &&
        signature->second.params.size() == arg_values.size()) {
      auto inst = lc.builder.MakeCall(resolved, arg_values, signature->second.result);
      return {inst->name, inst->type};
    }
    lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java call lowering requires a resolved method signature: " + callee_name);
    return {};
  }

  if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
    lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java field/member value lowering requires object-layout support");
    return {};
  }

  if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
    lc.diags.ReportError(new_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java object/array allocation requires JVM layout and GC support");
    return {};
  }

  if (auto cast = std::dynamic_pointer_cast<CastExpression>(expr)) {
    lc.diags.ReportError(cast->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java casts require checked numeric/reference conversion semantics");
    return {};
  }

  if (auto arr = std::dynamic_pointer_cast<ArrayAccessExpression>(expr)) {
    lc.diags.ReportError(arr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java array access requires JVM bounds and array-layout semantics");
    return {};
  }

  if (auto tern = std::dynamic_pointer_cast<TernaryExpression>(expr)) {
    lc.diags.ReportError(tern->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java conditional-expression lowering requires control-flow and phi nodes");
    return {};
  }

  if (std::dynamic_pointer_cast<SwitchExpression>(expr)) {
    lc.diags.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java switch-expression lowering is not supported by the current IR backend");
    return {};
  }

  lc.diags.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported Java expression reached IR lowering");
  return {};
}

/** @} */

/** @name Statement lowering */
/** @{ */

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
  if (!stmt || lc.terminated)
    return true;

  if (auto block = std::dynamic_pointer_cast<BlockStatement>(stmt)) {
    for (auto &s : block->statements) {
      if (!LowerStmt(s, lc))
        return false;
      if (lc.terminated)
        break;
    }
    return true;
  }

  if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) {
    if (!var->annotations.empty()) {
      lc.diags.ReportError(var->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Java local annotations require retention/processor metadata lowering");
      return false;
    }
    if (var->init) {
      auto init_val = EvalExpr(var->init, lc);
      if (init_val.type.kind == ir::IRTypeKind::kInvalid)
        return false;
      auto vt = var->type ? ToIRType(var->type, lc.diags, var->loc) : init_val.type;
      if (vt.kind == ir::IRTypeKind::kInvalid)
        return false;
      auto storage = lc.builder.MakeAlloca(vt);
      lc.builder.MakeStore(storage->name, init_val.value);
      lc.local_addresses[var->name] = storage->name;
      lc.env[var->name] = {init_val.value, vt};
    } else {
      auto vt = ToIRType(var->type, lc.diags, var->loc);
      if (vt.kind == ir::IRTypeKind::kInvalid)
        return false;
      lc.env[var->name] = {"", vt};
    }
    return true;
  }

  if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
    if (expr_stmt->expr &&
        EvalExpr(expr_stmt->expr, lc).type.kind == ir::IRTypeKind::kInvalid)
      return false;
    return true;
  }

  if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
    if (ret->value) {
      auto val = EvalExpr(ret->value, lc);
      if (val.type.kind == ir::IRTypeKind::kInvalid)
        return false;
      lc.builder.MakeReturn(val.value);
    } else {
      lc.builder.MakeReturn("");
    }
    lc.terminated = true;
    return true;
  }

  if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
    auto cond = EvalExpr(if_stmt->condition, lc);
    if (cond.type.kind == ir::IRTypeKind::kInvalid)
      return false;

    auto *then_block = lc.fn->CreateBlock("if.then");
    auto *else_block = if_stmt->else_body ? lc.fn->CreateBlock("if.else") : nullptr;
    auto *merge_block = lc.fn->CreateBlock("if.end");

    lc.builder.MakeCondBranch(cond.value, then_block, else_block ? else_block : merge_block);

    // Then block
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == then_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    LowerStmt(if_stmt->then_body, lc);
    bool then_term = lc.terminated;
    if (!then_term)
      lc.builder.MakeBranch(merge_block);

    // Else block
    bool else_term = false;
    if (else_block) {
      for (auto &bb : lc.fn->blocks) {
        if (bb.get() == else_block) {
          lc.builder.SetInsertPoint(bb);
          break;
        }
      }
      lc.terminated = false;
      LowerStmt(if_stmt->else_body, lc);
      else_term = lc.terminated;
      if (!else_term)
        lc.builder.MakeBranch(merge_block);
    }

    // Merge block
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == merge_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = else_block && then_term && else_term;
    if (lc.terminated) lc.builder.MakeUnreachable();
    return true;
  }

  if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
    auto *cond_block = lc.fn->CreateBlock("while.cond");
    auto *body_block = lc.fn->CreateBlock("while.body");
    auto *exit_block = lc.fn->CreateBlock("while.end");

    lc.builder.MakeBranch(cond_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == cond_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    auto cond = EvalExpr(while_stmt->condition, lc);
    lc.builder.MakeCondBranch(cond.value, body_block, exit_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == body_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    LowerStmt(while_stmt->body, lc);
    if (!lc.terminated)
      lc.builder.MakeBranch(cond_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == exit_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    return true;
  }

  if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
    lc.diags.ReportError(for_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java loop lowering requires storage/SSA semantics for mutable locals");
    if (for_stmt->init)
      LowerStmt(for_stmt->init, lc);

    auto *cond_block = lc.fn->CreateBlock("for.cond");
    auto *body_block = lc.fn->CreateBlock("for.body");
    auto *inc_block = lc.fn->CreateBlock("for.inc");
    auto *exit_block = lc.fn->CreateBlock("for.end");

    lc.builder.MakeBranch(cond_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == cond_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    if (for_stmt->condition) {
      auto cond = EvalExpr(for_stmt->condition, lc);
      lc.builder.MakeCondBranch(cond.value, body_block, exit_block);
    } else {
      lc.builder.MakeBranch(body_block);
    }

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == body_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    LowerStmt(for_stmt->body, lc);
    if (!lc.terminated)
      lc.builder.MakeBranch(inc_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == inc_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    if (for_stmt->update)
      EvalExpr(for_stmt->update, lc);
    lc.builder.MakeBranch(cond_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == exit_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    return true;
  }

  if (auto foreach_stmt = std::dynamic_pointer_cast<ForEachStatement>(stmt)) {
    lc.diags.ReportError(foreach_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java enhanced-for requires resolved iterator/array semantics");
    auto iterable = EvalExpr(foreach_stmt->iterable, lc);
    auto iter = lc.builder.MakeCall("__ploy_java_iterator", {iterable.value},
                                    ir::IRType::Pointer(ir::IRType::I8()), "");

    auto *cond_block = lc.fn->CreateBlock("foreach.cond");
    auto *body_block = lc.fn->CreateBlock("foreach.body");
    auto *exit_block = lc.fn->CreateBlock("foreach.end");

    lc.builder.MakeBranch(cond_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == cond_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    auto has_next = lc.builder.MakeCall("__ploy_java_has_next", {iter->name}, ir::IRType::I1(), "");
    lc.builder.MakeCondBranch(has_next->name, body_block, exit_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == body_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    ir::IRType elem_type = ToIRType(foreach_stmt->var_type, lc.diags, foreach_stmt->loc);
    auto current = lc.builder.MakeCall("__ploy_java_next", {iter->name}, elem_type, "");
    lc.env[foreach_stmt->var_name] = {current->name, elem_type};
    LowerStmt(foreach_stmt->body, lc);
    if (!lc.terminated)
      lc.builder.MakeBranch(cond_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == exit_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    return true;
  }

  if (auto switch_stmt = std::dynamic_pointer_cast<SwitchStatement>(stmt)) {
    lc.diags.ReportError(switch_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java switch-statement dispatch lowering is not implemented");
    return false;
  }

  if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
    lc.diags.ReportError(try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java exception-region lowering is not implemented");
    return false;
  }

  if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt)) {
    auto val = EvalExpr(throw_stmt->expr, lc);
    lc.builder.MakeCall("__ploy_java_throw", {val.value}, ir::IRType::Void(), "");
    lc.terminated = true;
    return true;
  }

  if (auto sync = std::dynamic_pointer_cast<SynchronizedStatement>(stmt)) {
    lc.diags.ReportError(sync->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java synchronized lowering requires exception-safe monitor regions");
    return false;
  }

  lc.diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported Java statement reached IR lowering");
  return false;
}

/** @} */

/** @name Declaration lowering */
/** @{ */

bool LowerMethod(const MethodDecl &method, LoweringContext &lc) {
  const bool has_parameter_annotations =
      std::any_of(method.params.begin(), method.params.end(),
                  [](const Parameter &parameter) { return !parameter.annotations.empty(); });
  if (!method.annotations.empty() || has_parameter_annotations) {
    lc.diags.ReportError(
        method.loc, frontends::ErrorCode::kUnsupportedLowering,
        "Java method/parameter annotations require retention and annotation-metadata lowering");
    return false;
  }
  if (!method.type_params.empty() || method.is_synchronized) {
    lc.diags.ReportError(
        method.loc, frontends::ErrorCode::kUnsupportedLowering,
        method.is_synchronized
            ? "Java synchronized method lowering requires exception-safe monitor regions"
            : "generic Java method lowering requires erasure/bridge dispatch support");
    return false;
  }
  std::string mangled =
      lc.current_class.empty() ? method.name : lc.current_class + "::" + method.name;

  ir::IRType ret = ToIRType(method.return_type, lc.diags, method.loc);
  if (ret.kind == ir::IRTypeKind::kInvalid)
    return false;

  std::vector<std::pair<std::string, ir::IRType>> params;
  if (!method.is_static && !lc.current_class.empty()) {
    params.push_back({"this", ir::IRType::Pointer(ir::IRType::I8())});
  }
  for (auto &p : method.params) {
    if (p.is_varargs) {
      lc.diags.ReportError(method.loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Java varargs calling convention is not supported by lowering");
      return false;
    }
    auto type = ToIRType(p.type, lc.diags, method.loc);
    if (type.kind == ir::IRTypeKind::kInvalid)
      return false;
    params.push_back({p.name, type});
  }

  lc.fn = lc.ir_ctx.CreateFunction(mangled, ret, params);
  lc.builder.SetCurrentFunction(lc.fn);
  auto *entry = lc.fn->CreateBlock("entry");
  lc.fn->entry = entry;
  if (!lc.fn->blocks.empty()) {
    lc.builder.SetInsertPoint(lc.fn->blocks.back());
  }

  lc.env.clear();
  lc.local_addresses.clear();
  for (auto &p : params) {
    lc.env[p.first] = {p.first, p.second};
    // Parameters are assignable locals; stores keep mutations correct across CFG edges.
    auto storage = lc.builder.MakeAlloca(p.second, p.first + ".addr");
    lc.builder.MakeStore(storage->name, p.first);
    lc.local_addresses[p.first] = storage->name;
  }
  lc.terminated = false;

  for (auto &s : method.body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
  }

  if (!lc.terminated) {
    if (ret.kind == ir::IRTypeKind::kVoid) {
      lc.builder.MakeReturn("");
    } else {
      lc.diags.ReportError(method.loc, frontends::ErrorCode::kUnsupportedLowering,
                           "non-void Java method can reach the end without a lowered return");
      return false;
    }
  }
  return true;
}

bool LowerConstructor(const ConstructorDecl &ctor, LoweringContext &lc) {
  const bool has_parameter_annotations =
      std::any_of(ctor.params.begin(), ctor.params.end(),
                  [](const Parameter &parameter) { return !parameter.annotations.empty(); });
  if (!ctor.annotations.empty() || has_parameter_annotations) {
    lc.diags.ReportError(
        ctor.loc, frontends::ErrorCode::kUnsupportedLowering,
        "Java constructor/parameter annotations require retention metadata lowering");
    return false;
  }
  if (ctor.invocation_kind != ConstructorDecl::InvocationKind::kNone) {
    lc.diags.ReportError(
        ctor.loc, frontends::ErrorCode::kUnsupportedLowering,
        ctor.has_flexible_body
            ? "Java 25 flexible constructor bodies require explicit prologue/invocation lowering"
            : "explicit Java this/super constructor invocation lowering is not implemented");
    return false;
  }
  std::string mangled = lc.current_class + "::<init>";

  std::vector<std::pair<std::string, ir::IRType>> params;
  params.push_back({"this", ir::IRType::Pointer(ir::IRType::I8())});
  for (auto &p : ctor.params) {
    if (p.is_varargs) {
      lc.diags.ReportError(ctor.loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Java varargs constructor lowering is not supported");
      return false;
    }
    auto type = ToIRType(p.type, lc.diags, ctor.loc);
    if (type.kind == ir::IRTypeKind::kInvalid)
      return false;
    params.push_back({p.name, type});
  }

  lc.fn = lc.ir_ctx.CreateFunction(mangled, ir::IRType::Void(), params);
  lc.builder.SetCurrentFunction(lc.fn);
  auto *entry = lc.fn->CreateBlock("entry");
  lc.fn->entry = entry;
  if (!lc.fn->blocks.empty()) {
    lc.builder.SetInsertPoint(lc.fn->blocks.back());
  }

  lc.env.clear();
  lc.local_addresses.clear();
  for (auto &p : params) {
    lc.env[p.first] = {p.first, p.second};
    // Parameters are assignable locals; stores keep mutations correct across CFG edges.
    auto storage = lc.builder.MakeAlloca(p.second, p.first + ".addr");
    lc.builder.MakeStore(storage->name, p.first);
    lc.local_addresses[p.first] = storage->name;
  }
  lc.terminated = false;

  for (auto &s : ctor.body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
  }

  if (!lc.terminated) {
    lc.builder.MakeReturn("");
  }
  return true;
}

void LowerClass(const ClassDecl &cls, LoweringContext &lc) {
  if (!cls.annotations.empty()) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java class annotations require retention/processor metadata lowering");
    return;
  }
  if (!cls.type_params.empty() || cls.superclass || !cls.interfaces.empty() || cls.is_sealed ||
      cls.is_non_sealed || !cls.permits.empty()) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "generic/inherited/sealed Java class runtime semantics are not implemented");
    return;
  }
  auto saved_class = lc.current_class;
  lc.current_class = cls.name;
  for (const auto &member : cls.members) {
    auto method = std::dynamic_pointer_cast<MethodDecl>(member);
    if (!method || !method->is_static) continue;
    const auto name = cls.name + "::" + method->name;
    if (lc.static_methods.count(name)) { lc.static_methods[name].ambiguous = true; continue; }
    LoweringContext::StaticSignature signature;
    signature.result = ToIRType(method->return_type, lc.diags, method->loc);
    for (const auto &param : method->params)
      signature.params.push_back(ToIRType(param.type, lc.diags, method->loc));
    lc.static_methods.emplace(name, std::move(signature));
  }


  for (auto &member : cls.members) {
    if (auto method = std::dynamic_pointer_cast<MethodDecl>(member)) {
      if (!method->is_abstract && !method->is_native) {
        LowerMethod(*method, lc);
      }
    } else if (auto ctor = std::dynamic_pointer_cast<ConstructorDecl>(member)) {
      LowerConstructor(*ctor, lc);
    } else if (auto field = std::dynamic_pointer_cast<FieldDecl>(member)) {
      lc.diags.ReportError(field->loc, frontends::ErrorCode::kUnsupportedLowering,
                           field->is_static
                               ? "Java static fields require class storage/<clinit> lowering"
                               : "Java instance fields require JVM object-layout lowering");
    } else if (auto inner = std::dynamic_pointer_cast<ClassDecl>(member)) {
      LowerClass(*inner, lc);
    } else if (std::dynamic_pointer_cast<InterfaceDecl>(member) ||
               std::dynamic_pointer_cast<EnumDecl>(member) ||
               std::dynamic_pointer_cast<RecordDecl>(member)) {
      lc.diags.ReportError(
          member->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Java nested interface/enum/record lowering is not implemented by the current IR backend");
    } else {
      lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported Java class member reached IR lowering");
    }
  }

  lc.current_class = saved_class;
}

void LowerEnum(const EnumDecl &en, LoweringContext &lc) {
  const bool has_constant_semantics =
      std::any_of(en.constants.begin(), en.constants.end(), [](const EnumDecl::EnumConstant &value) {
        return !value.annotations.empty() || !value.args.empty() || !value.body.empty();
      });
  if (!en.annotations.empty() || has_constant_semantics || !en.members.empty() ||
      !en.interfaces.empty()) {
    lc.diags.ReportError(en.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Java annotated/stateful enum lowering requires JVM enum metadata");
    return;
  }
  int ordinal = 0;
  for (auto &c : en.constants) {
    lc.env[en.name + "::" + c.name] = {std::to_string(ordinal), ir::IRType::I32(true)};
    ++ordinal;
  }
}

void LowerRecord(const RecordDecl &rec, LoweringContext &lc) {
  // A faithful record implementation needs an object layout, component
  // stores, generated accessors, equals/hashCode/toString, and constructor
  // validation.  The previous placeholder emitted accessors that always
  // returned zero, which was observably wrong.  Fail closed until that model
  // exists.
  lc.diags.ReportError(rec.loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Java record lowering requires record object-layout support");
}

} // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
  LoweringContext lc(ctx, diags);

  if (module.is_compact_source) {
    const auto loc = module.declarations.empty() ? core::SourceLoc{}
                                                  : module.declarations.front()->loc;
    diags.ReportError(loc, frontends::ErrorCode::kUnsupportedLowering,
                      "Java 25 compact source files require implicit-class metadata lowering");
    return;
  }

  for (const auto &decl : module.declarations) {
    if (auto descriptor = std::dynamic_pointer_cast<ModuleDecl>(decl)) {
      diags.ReportError(descriptor->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Java module descriptors require module metadata emission");
    } else if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
      LowerClass(*cls, lc);
    } else if (auto iface = std::dynamic_pointer_cast<InterfaceDecl>(decl)) {
      diags.ReportError(iface->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Java interface/default-method dispatch lowering is not implemented");
    } else if (auto en = std::dynamic_pointer_cast<EnumDecl>(decl)) {
      LowerEnum(*en, lc);
    } else if (auto rec = std::dynamic_pointer_cast<RecordDecl>(decl)) {
      LowerRecord(*rec, lc);
    } else {
      diags.ReportError(decl->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "unsupported Java top-level declaration reached IR lowering");
    }
  }
}

} // namespace polyglot::java

/** @} */
