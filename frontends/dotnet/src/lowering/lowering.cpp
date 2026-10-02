#include "frontends/common/include/native_builtins.h"
#include "frontends/common/include/native_string_literal.h"
/**
 * @file     lowering.cpp
 * @brief    .NET/C# language frontend implementation
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <cstdlib>
#include <string>
#include <unordered_map>

#include "middle/include/ir/ir_builder.h"

#include "frontends/dotnet/include/dotnet_lowering.h"

namespace polyglot::dotnet {
namespace {

using Name = std::string;

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &node, frontends::Diagnostics &diags,
                    const core::SourceLoc &fallback_loc) {
  if (!node) {
    diags.ReportError(fallback_loc, frontends::ErrorCode::kUnsupportedLowering,
                      "C# lowering requires a resolved runtime type");
    return ir::IRType::Invalid();
  }
  if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
    auto &n = simple->name;
    if (n == "sbyte")
      return ir::IRType::I8(true);
    if (n == "byte")
      return ir::IRType::I8(false);
    if (n == "short")
      return ir::IRType::I16(true);
    if (n == "ushort")
      return ir::IRType::I16(false);
    if (n == "int")
      return ir::IRType::I32(true);
    if (n == "uint")
      return ir::IRType::I32(false);
    if (n == "long")
      return ir::IRType::I64(true);
    if (n == "ulong")
      return ir::IRType::I64(false);
    if (n == "nint")
      return ir::IRType::I64(true);
    if (n == "nuint")
      return ir::IRType::I64(false);
    if (n == "float")
      return ir::IRType::F32();
    if (n == "double")
      return ir::IRType::F64();
    if (n == "decimal") {
      diags.ReportError(node->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "C# decimal cannot be represented as binary f64");
      return ir::IRType::Invalid();
    }
    if (n == "bool")
      return ir::IRType::I1();
    if (n == "char")
      return ir::IRType::I16(false);
    if (n == "void")
      return ir::IRType::Void();
    if (n == "string" || n == "System.String" || n == "object" || n == "System.Object")
      return ir::IRType::Pointer(ir::IRType::I8());
    return ir::IRType::Pointer(ir::IRType::I8()); // reference types
  }
  if (auto arr = std::dynamic_pointer_cast<ArrayType>(node)) {
    if (arr->rank != 1) {
      diags.ReportError(arr->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "multidimensional C# array layout is not supported");
      return ir::IRType::Invalid();
    }
    auto element = ToIRType(arr->element_type, diags, arr->loc);
    return element.kind == ir::IRTypeKind::kInvalid ? element : ir::IRType::Pointer(element);
  }
  if (auto nullable = std::dynamic_pointer_cast<NullableType>(node)) {
    diags.ReportError(nullable->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "C# nullable values require a tagged representation");
    return ir::IRType::Invalid();
  }
  diags.ReportError(node->loc, frontends::ErrorCode::kUnsupportedLowering,
                    "unsupported C# runtime type reached IR lowering");
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
  if (end == text.c_str() || (*end != '\0' && *end != 'f' && *end != 'F' && *end != 'd' &&
                              *end != 'D' && *end != 'm' && *end != 'M'))
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

bool MapBinOp(const std::string &op, const ir::IRType &type,
              ir::BinaryInstruction::Op &mapped) {
  const bool is_float =
      type.kind == ir::IRTypeKind::kF32 || type.kind == ir::IRTypeKind::kF64;
  if (op == "+")
    mapped = is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
  if (op == "-")
    mapped = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
  if (op == "*")
    mapped = is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
  if (op == "/")
    mapped = is_float ? ir::BinaryInstruction::Op::kFDiv
                      : (type.is_signed ? ir::BinaryInstruction::Op::kSDiv
                                        : ir::BinaryInstruction::Op::kUDiv);
  if (op == "%")
    mapped = is_float ? ir::BinaryInstruction::Op::kFRem
                      : (type.is_signed ? ir::BinaryInstruction::Op::kSRem
                                        : ir::BinaryInstruction::Op::kURem);
  if (op == "&")
    mapped = ir::BinaryInstruction::Op::kAnd;
  if (op == "|")
    mapped = ir::BinaryInstruction::Op::kOr;
  if (op == "^")
    mapped = ir::BinaryInstruction::Op::kXor;
  if (op == "<<")
    mapped = ir::BinaryInstruction::Op::kShl;
  if (op == ">>")
    mapped = type.is_signed ? ir::BinaryInstruction::Op::kAShr
                            : ir::BinaryInstruction::Op::kLShr;
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
                      : (type.is_signed ? ir::BinaryInstruction::Op::kCmpSlt
                                        : ir::BinaryInstruction::Op::kCmpUlt);
  if (op == "<=")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFle
                      : (type.is_signed ? ir::BinaryInstruction::Op::kCmpSle
                                        : ir::BinaryInstruction::Op::kCmpUle);
  if (op == ">")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFgt
                      : (type.is_signed ? ir::BinaryInstruction::Op::kCmpSgt
                                        : ir::BinaryInstruction::Op::kCmpUgt);
  if (op == ">=")
    mapped = is_float ? ir::BinaryInstruction::Op::kCmpFge
                      : (type.is_signed ? ir::BinaryInstruction::Op::kCmpSge
                                        : ir::BinaryInstruction::Op::kCmpUge);
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
  case ir::BinaryInstruction::Op::kCmpUlt:
  case ir::BinaryInstruction::Op::kCmpUle:
  case ir::BinaryInstruction::Op::kCmpUgt:
  case ir::BinaryInstruction::Op::kCmpUge:
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
                           "use of an uninitialized C# local in IR lowering: " + id->name);
      return {};
    }
    lc.diags.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unresolved C# value in IR lowering: " + id->name);
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

    if (!v.empty() && v[0] == '$') {
      lc.diags.ReportError(
          lit->loc, frontends::ErrorCode::kUnsupportedLowering,
          "C# interpolated-string lowering is not supported by the current IR backend");
      return {};
    }

    if (v.starts_with("\"\"\"")) {
      lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C# raw-string value normalization is not implemented");
      return {};
    }

    if ((!v.empty() && v[0] == '"') || v.starts_with("@\"")) {
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
      auto name = lc.builder.MakeStringLiteral(bytes, "csstr");
      return {name, ir::IRType::Pointer(ir::IRType::I8())};
    }

    long long iv;
    if (IsIntegerLiteral(v, &iv))
      return MakeLiteral(iv, lc);

    double fv;
    if (IsFloatLiteral(v, &fv)) {
      if (v.ends_with("m") || v.ends_with("M")) {
        lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "C# decimal literals cannot be represented as binary f64");
        return {};
      }
      return MakeFloatLiteral(fv, lc);
    }

    if (!v.empty() && v[0] == '\'') {
      if (v.size() == 3)
        return MakeLiteral(static_cast<long long>(v[1]), lc);
      lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "escaped C# character literal lowering is not implemented");
      return {};
    }

    lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C# literal reached IR lowering: " + v);
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
                         "unsupported C# unary operator in lowering: " + unary->op);
    return {};
  }

  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    if (bin->op == "=") {
      auto rhs = EvalExpr(bin->right, lc);
      if (rhs.type.kind == ir::IRTypeKind::kInvalid)
        return {};
      if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
        if (!lc.env.contains(id->name)) {
          lc.diags.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "C# field or unresolved assignment target requires storage lowering: " +
                                   id->name);
          return {};
        }
        if (lc.local_addresses.count(id->name))
          lc.builder.MakeStore(lc.local_addresses.at(id->name), rhs.value);
        else lc.env[id->name] = {rhs.value, rhs.type};
      } else {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "C# lowering only supports identifier assignment targets");
        return {};
      }
      return rhs;
    }

    if (bin->op == "+=" || bin->op == "-=" || bin->op == "*=" || bin->op == "/=" ||
        bin->op == "%=") {
      auto left = EvalExpr(bin->left, lc);
      auto right = EvalExpr(bin->right, lc);
      if (left.type.kind == ir::IRTypeKind::kInvalid ||
          right.type.kind == ir::IRTypeKind::kInvalid)
        return {};
      std::string base_op = bin->op.substr(0, bin->op.size() - 1);
      ir::BinaryInstruction::Op op;
      if (!MapBinOp(base_op, left.type, op)) {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "unsupported C# binary operator: " + base_op);
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
                             "C# lowering only supports identifier compound-assignment targets");
        return {};
      }
      return {result->name, left.type};
    }
    if (bin->op == "&=" || bin->op == "|=" || bin->op == "^=" || bin->op == "<<=" ||
        bin->op == ">>=" || bin->op == ">>>=" || bin->op == "\?\?=") {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported C# compound assignment operator: " + bin->op);
      return {};
    }

    if (bin->op == "&&" || bin->op == "||") {
      auto left = EvalExpr(bin->left, lc);
      if (left.type.kind != ir::IRTypeKind::kI1) {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "C# logical operands must be boolean");
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
                             "C# logical operands must be boolean");
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

    if (left.type.kind == ir::IRTypeKind::kPointer && bin->op != "==" && bin->op != "!=") {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C# reference/string operators require managed runtime semantics");
      return {};
    }
    ir::BinaryInstruction::Op op;
    if (!MapBinOp(bin->op, left.type, op)) {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported C# binary operator: " + bin->op);
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
                           "indirect C# calls are not supported by lowering");
      return {};
    }

    // Map well-known .NET methods
    if (callee_name == "Console.WriteLine" || callee_name == "Console.Write") {
      auto inst = lc.builder.MakeCall("__ploy_dotnet_print", arg_values, ir::IRType::Void(), "");
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
                         "C# call lowering requires a resolved method signature: " + callee_name);
    return {};
  }

  if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
    lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# member-value lowering requires object-layout support");
    return {};
  }

  if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
    lc.diags.ReportError(new_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# object/array allocation requires managed layout and GC support");
    return {};
  }

  if (auto cast = std::dynamic_pointer_cast<CastExpression>(expr)) {
    lc.diags.ReportError(cast->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# casts require checked numeric/reference conversion semantics");
    return {};
  }

  if (auto index = std::dynamic_pointer_cast<IndexExpression>(expr)) {
    lc.diags.ReportError(index->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# indexing requires array/span bounds and layout semantics");
    return {};
  }

  if (auto tern = std::dynamic_pointer_cast<TernaryExpression>(expr)) {
    lc.diags.ReportError(tern->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# conditional-expression lowering requires control-flow and phi nodes");
    return {};
  }

  if (auto nc = std::dynamic_pointer_cast<NullCoalescingExpression>(expr)) {
    lc.diags.ReportError(nc->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# null-coalescing lowering requires short-circuit control flow");
    return {};
  }

  if (auto is_expr = std::dynamic_pointer_cast<IsExpression>(expr)) {
    lc.diags.ReportError(is_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# type tests require managed runtime type metadata");
    return {};
  }

  if (auto as_expr = std::dynamic_pointer_cast<AsExpression>(expr)) {
    lc.diags.ReportError(as_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# 'as' conversion requires checked runtime type semantics");
    return {};
  }

  if (auto await = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
    lc.diags.ReportError(await->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# await lowering requires an async state machine");
    return {};
  }

  if (auto switch_expr = std::dynamic_pointer_cast<SwitchExpression>(expr)) {
    lc.diags.ReportError(
        switch_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
        "C# switch expressions require pattern dispatch and merge-value control flow");
    return {};
  }

  lc.diags.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported C# expression reached IR lowering");
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
                         "C# loop lowering requires storage/SSA semantics for mutable locals");
    return false;
  }

  if (auto foreach_stmt = std::dynamic_pointer_cast<ForEachStatement>(stmt)) {
    lc.diags.ReportError(foreach_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# foreach requires resolved enumerator and disposal semantics");
    return false;
  }

  if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
    lc.diags.ReportError(try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# exception-region lowering is not implemented");
    return false;
  }

  if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt)) {
    if (throw_stmt->expr) {
      auto val = EvalExpr(throw_stmt->expr, lc);
      lc.builder.MakeCall("__ploy_dotnet_throw", {val.value}, ir::IRType::Void(), "");
    } else {
      lc.builder.MakeCall("__ploy_dotnet_rethrow", {}, ir::IRType::Void(), "");
    }
    lc.terminated = true;
    return true;
  }

  if (auto using_stmt = std::dynamic_pointer_cast<UsingStatement>(stmt)) {
    lc.diags.ReportError(using_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# using lowering requires exception-safe disposal regions");
    return false;
  }

  if (auto lock_stmt = std::dynamic_pointer_cast<LockStatement>(stmt)) {
    lc.diags.ReportError(lock_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# lock lowering requires exception-safe monitor regions");
    return false;
  }

  if (auto switch_stmt = std::dynamic_pointer_cast<SwitchStatement>(stmt)) {
    lc.diags.ReportError(switch_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# switch dispatch lowering is not implemented");
    return false;
  }

  lc.diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported C# statement reached IR lowering");
  return false;
}

/** @} */

/** @name Declaration lowering */
/** @{ */

bool LowerMethod(const MethodDecl &method, LoweringContext &lc) {
  if (method.is_async || !method.type_params.empty()) {
    lc.diags.ReportError(
        method.loc, frontends::ErrorCode::kUnsupportedLowering,
        method.is_async ? "C# async method lowering requires a state machine"
                        : "generic C# method lowering requires runtime generic specialization");
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
    if (p.is_ref || p.is_out || p.is_in || p.is_params || p.is_this || p.default_value) {
      lc.diags.ReportError(p.type ? p.type->loc : method.loc,
                           frontends::ErrorCode::kUnsupportedLowering,
                           "C# parameter passing/default semantics are not represented by this IR lowering");
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

  // Expression body (e.g., int Foo() => 42;)
  if (method.expression_body) {
    auto val = EvalExpr(method.expression_body, lc);
    if (val.type.kind == ir::IRTypeKind::kInvalid)
      return false;
    if (ret.kind != ir::IRTypeKind::kVoid) {
      lc.builder.MakeReturn(val.value);
    } else {
      lc.builder.MakeReturn("");
    }
    return true;
  }

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
                           "non-void C# method can reach the end without a lowered return");
      return false;
    }
  }
  return true;
}

bool LowerConstructor(const ConstructorDecl &ctor, LoweringContext &lc) {
  if (ctor.is_partial) {
    lc.diags.ReportError(ctor.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# partial-constructor lowering is not implemented");
    return false;
  }
  std::string mangled = lc.current_class + "::.ctor";

  std::vector<std::pair<std::string, ir::IRType>> params;
  params.push_back({"this", ir::IRType::Pointer(ir::IRType::I8())});
  for (auto &p : ctor.params) {
    if (p.is_ref || p.is_out || p.is_in || p.is_params || p.default_value) {
      lc.diags.ReportError(ctor.loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C# constructor parameter passing/default semantics are not supported");
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

  // Base/this initializer call
  if (!ctor.initializer_kind.empty()) {
    lc.diags.ReportError(ctor.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# constructor chaining requires resolved constructor dispatch");
    return false;
  }

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
  if (cls.is_record) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# record class lowering requires synthesized record semantics");
    return;
  }
  if (!cls.primary_ctor_params.empty()) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# primary-constructor lowering is not implemented");
    return;
  }
  if (!cls.type_params.empty() || cls.base_type || !cls.interfaces.empty()) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "generic/inherited C# class runtime semantics are not implemented");
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
      if (!method->is_abstract && !method->is_extern) {
        LowerMethod(*method, lc);
      }
    } else if (auto ctor = std::dynamic_pointer_cast<ConstructorDecl>(member)) {
      LowerConstructor(*ctor, lc);
    } else if (auto field = std::dynamic_pointer_cast<FieldDecl>(member)) {
      lc.diags.ReportError(field->loc, frontends::ErrorCode::kUnsupportedLowering,
                           field->is_static
                               ? "C# static fields require type storage/initializer lowering"
                               : "C# instance fields require managed object-layout lowering");
    } else if (auto property = std::dynamic_pointer_cast<PropertyDecl>(member)) {
      lc.diags.ReportError(
          property->loc, frontends::ErrorCode::kUnsupportedLowering,
          property->uses_field_keyword
              ? "C# 14 field-backed properties require synthesized backing-field semantics"
              : "C# properties require managed accessor and object-layout lowering");
    } else if (auto op = std::dynamic_pointer_cast<OperatorDecl>(member)) {
      lc.diags.ReportError(
          op->loc, frontends::ErrorCode::kUnsupportedLowering,
          op->is_compound_assignment
              ? "C# 14 compound-assignment operators require operator dispatch lowering"
              : "C# user-defined operators require operator dispatch lowering");
    } else if (auto extension = std::dynamic_pointer_cast<ExtensionDecl>(member)) {
      lc.diags.ReportError(extension->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C# 14 extension members require extension-binding metadata");
    } else if (auto inner = std::dynamic_pointer_cast<ClassDecl>(member)) {
      LowerClass(*inner, lc);
    } else if (std::dynamic_pointer_cast<StructDecl>(member) ||
               std::dynamic_pointer_cast<InterfaceDecl>(member)) {
      lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "nested C# struct/interface lowering is not implemented");
    } else {
      lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported C# class member reached IR lowering");
    }
  }

  lc.current_class = saved_class;
}

void LowerEnum(const EnumDecl &en, LoweringContext &lc) {
  int ordinal = 0;
  for (auto &m : en.members) {
    lc.env[en.name + "::" + m.name] = {std::to_string(ordinal), ir::IRType::I32(true)};
    ++ordinal;
  }
}

void LowerNamespace(const NamespaceDecl &ns, LoweringContext &lc);
void LowerDecl(const std::shared_ptr<Statement> &decl, LoweringContext &lc);

void LowerNamespace(const NamespaceDecl &ns, LoweringContext &lc) {
  for (auto &member : ns.members) {
    LowerDecl(member, lc);
  }
}

void LowerDecl(const std::shared_ptr<Statement> &decl, LoweringContext &lc) {
  if (!decl)
    return;
  if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
    LowerClass(*cls, lc);
  } else if (auto st = std::dynamic_pointer_cast<StructDecl>(decl)) {
    lc.diags.ReportError(
        st->loc, frontends::ErrorCode::kUnsupportedLowering,
        st->is_record ? "C# record struct lowering requires synthesized record semantics"
                      : "C# struct lowering is not implemented by the current IR backend");
  } else if (auto iface = std::dynamic_pointer_cast<InterfaceDecl>(decl)) {
    lc.diags.ReportError(iface->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C# interface lowering is not implemented by the current IR backend");
  } else if (auto en = std::dynamic_pointer_cast<EnumDecl>(decl)) {
    LowerEnum(*en, lc);
  } else if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
    LowerNamespace(*ns, lc);
  } else {
    lc.diags.ReportError(decl->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C# top-level declaration reached IR lowering");
  }
}

} // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
  LoweringContext lc(ctx, diags);

  if (!module.file_directives.empty()) {
    diags.ReportError(module.file_directives.front()->loc,
                      frontends::ErrorCode::kUnsupportedLowering,
                      "C# 14 file-based application directives require SDK/package resolution");
  }

  for (const auto &decl : module.declarations) {
    LowerDecl(decl, lc);
  }

  // Lower top-level statements into a synthetic Main method (C# 9.0+)
  if (!module.top_level_statements.empty()) {
    diags.ReportError(module.top_level_statements.front()->loc,
                      frontends::ErrorCode::kUnsupportedLowering,
                      "C# top-level statement lowering is not implemented");
  }
}

} // namespace polyglot::dotnet

/** @} */
