#include "frontends/common/include/native_builtins.h"
#include "frontends/common/include/native_string_literal.h"
/**
 * @file     lowering.cpp
 * @brief    JavaScript - Polyglot IR lowering
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Translates a JavaScript AST into the Polyglot IR.  The lowering targets
 * a typed numeric subset suitable for cross-language interop:
 *   - Top-level function declarations become IR functions
 *   - Function parameters and returns require supported JSDoc annotations
 *   - Selected arithmetic, strict comparison, unary and bitwise operations
 *     lower when operand types prove the primitive semantics are equivalent
 *   - Variable declarations become alloca/store pairs
 *   - if/while/for/return statements lower to control-flow blocks
 *   - Calls resolve only against pre-collected, fully typed JS signatures
 *
 * Constructs that have no clean static lowering (closures, classes,
 * generators, async/await, prototype chains) produce E4003 diagnostics.
 * Frontend lowering therefore fails closed instead of emitting executable IR
 * with approximated JavaScript semantics.
 */
#include <memory>
#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/ir_builder.h"

#include "common/include/core/types.h"
#include "frontends/javascript/include/javascript_lowering.h"

namespace polyglot::javascript {

namespace {

// Map a JSDoc TypeNode to an IR primitive type. Missing and unknown types are
// deliberately invalid: JavaScript's dynamic values cannot be modeled as f64.
ir::IRType ToIRType(const std::shared_ptr<TypeNode> &t) {
  if (!t)
    return ir::IRType::Invalid();
  if (auto nt = std::dynamic_pointer_cast<NamedType>(t)) {
    const std::string &n = nt->name;
    if (n == "number" || n == "Number")
      return ir::IRType::F64();
    if (n == "integer" || n == "int" || n == "i32")
      return ir::IRType::I32(true);
    if (n == "i64" || n == "long")
      return ir::IRType::I64(true);
    if (n == "u32")
      return ir::IRType::I32(false);
    if (n == "u64")
      return ir::IRType::I64(false);
    if (n == "f32" || n == "float")
      return ir::IRType::F32();
    if (n == "f64" || n == "double")
      return ir::IRType::F64();
    if (n == "boolean" || n == "Boolean" || n == "bool")
      return ir::IRType::I1();
    if (n == "string" || n == "String")
      return ir::IRType::Pointer(ir::IRType::I8());
    if (n == "void" || n == "undefined" || n == "null")
      return ir::IRType::Void();
    if (n == "bigint" || n == "BigInt")
      return ir::IRType::I64(true);
    if (n == "any" || n == "unknown" || n == "object" || n == "Object")
      return ir::IRType::Pointer(ir::IRType::I8());
    return ir::IRType::Invalid();
  }
  if (auto gt = std::dynamic_pointer_cast<GenericType>(t)) {
    if (gt->name == "Promise" || gt->name == "Array" || gt->name == "Map" || gt->name == "Set") {
      return ir::IRType::Pointer(ir::IRType::I8());
    }
    return ir::IRType::Invalid();
  }
  if (std::dynamic_pointer_cast<UnionType>(t)) {
    return ir::IRType::Invalid();
  }
  return ir::IRType::Invalid();
}

class Lowerer {
  struct FunctionSignature {
    ir::IRType return_type{ir::IRType::Invalid()};
    std::vector<ir::IRType> param_types;
  };

public:
  Lowerer(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag) :
      module_(mod), ctx_(ctx), builder_(ctx), diag_(diag) {}

  void Run() {
    for (auto &s : module_.body) {
      if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(s)) {
        RegisterFunctionSignature(*fd);
      } else if (auto exp = std::dynamic_pointer_cast<ExportDecl>(s)) {
        if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(exp->declaration))
          RegisterFunctionSignature(*fd);
      }
    }

    // Lower each top-level function declaration; everything else is
    // reported but not embedded in IR (we focus on the FFI surface).
    for (auto &s : module_.body) {
      if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(s)) {
        LowerFunction(*fd);
      } else if (auto cd = std::dynamic_pointer_cast<ClassDecl>(s)) {
        RejectUnlowerableClassFeatures(*cd);
      } else if (auto exp = std::dynamic_pointer_cast<ExportDecl>(s)) {
        if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(exp->declaration)) {
          LowerFunction(*fd);
        } else if (auto cd = std::dynamic_pointer_cast<ClassDecl>(exp->declaration)) {
          RejectUnlowerableClassFeatures(*cd);
        } else if (exp->declaration || exp->default_expr) {
          diag_.ReportError(exp->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "executable JavaScript export has no module-initializer lowering");
        }
      } else if (!std::dynamic_pointer_cast<ImportDecl>(s)) {
        diag_.ReportError(s->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript top-level statement has no module-initializer lowering");
      }
    }
  }

private:
  void RegisterFunctionSignature(const FunctionDecl &fd) {
    FunctionSignature signature;
    signature.return_type = ToIRType(fd.return_type);
    for (const auto &param : fd.params)
      signature.param_types.push_back(ToIRType(param.type));
    function_signatures_[fd.name] = std::move(signature);
  }

  void RejectUnlowerableClassFeatures(const ClassDecl &cls) {
    for (const auto &member : cls.members) {
      if (std::dynamic_pointer_cast<StaticBlock>(member) ||
          std::dynamic_pointer_cast<FieldDecl>(member)) {
        diag_.ReportError(
            member->loc, frontends::ErrorCode::kUnsupportedLowering,
            "JavaScript class fields/static blocks are parsed and analyzed but require "
            "dynamic runtime lowering");
        return;
      }
      if (auto method = std::dynamic_pointer_cast<MethodDecl>(member); method && method->is_private) {
        diag_.ReportError(method->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript private methods require dynamic runtime lowering");
        return;
      }
    }
    diag_.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                      "JavaScript classes require dynamic prototype/runtime lowering");
  }

  // ---- Function lowering -------------------------------------------------

  void LowerFunction(const FunctionDecl &fd) {
    if (fd.is_async || fd.is_generator) {
      diag_.ReportError(fd.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript " + std::string(fd.is_async ? "async" : "generator") +
                            " function '" + fd.name + "' requires runtime lowering");
      return;
    }

    for (const auto &param : fd.params) {
      if (param.rest || param.default_value) {
        diag_.ReportError(fd.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript rest/default parameters require call-protocol lowering");
        return;
      }
      if (!param.type || ToIRType(param.type).kind == ir::IRTypeKind::kInvalid) {
        diag_.ReportError(fd.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript parameter '" + param.name +
                              "' requires a supported JSDoc type for static lowering");
        return;
      }
    }
    if (!fd.return_type || ToIRType(fd.return_type).kind == ir::IRTypeKind::kInvalid) {
      diag_.ReportError(fd.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript function '" + fd.name +
                            "' requires a supported JSDoc return type for static lowering");
      return;
    }

    std::vector<std::pair<std::string, ir::IRType>> ir_params;
    ir_params.reserve(fd.params.size());
    for (auto &p : fd.params) {
      if (p.rest)
        continue;
      ir_params.emplace_back(p.name, ToIRType(p.type));
    }
    auto ret_ty = ToIRType(fd.return_type);

    auto fn = ctx_.CreateFunction(fd.name, ret_ty, ir_params);
    builder_.SetCurrentFunction(fn);
    auto entry = fn->CreateBlock("entry");
    builder_.SetInsertPoint(entry);

    // Allocate parameter slots so they behave like normal locals.
    locals_.clear();
    for (auto &p : ir_params) {
      auto a = builder_.MakeAlloca(p.second, p.first + ".addr");
      builder_.MakeStore(p.first + ".addr", p.first);
      locals_[p.first] = {p.first + ".addr", p.second};
    }
    current_ret_ = ret_ty;
    terminated_ = false;

    if (auto blk = std::dynamic_pointer_cast<BlockStatement>(fd.body)) {
      for (auto &s : blk->statements) {
        if (terminated_)
          break;
        LowerStatement(s);
      }
    }
    if (!terminated_) {
      if (ret_ty.kind == ir::IRTypeKind::kVoid) {
        builder_.MakeReturn();
      } else {
        diag_.ReportError(fd.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript non-void function may complete without a return value");
        builder_.MakeUnreachable();
      }
    }
    builder_.ClearCurrentFunction();
  }

  // ---- Statement lowering ------------------------------------------------

  void LowerStatement(const std::shared_ptr<Statement> &s) {
    if (!s || terminated_)
      return;
    if (auto blk = std::dynamic_pointer_cast<BlockStatement>(s)) {
      for (auto &c : blk->statements) {
        if (terminated_)
          return;
        LowerStatement(c);
      }
      return;
    }
    if (auto vd = std::dynamic_pointer_cast<VariableDecl>(s)) {
      if (vd->kind == "using" || vd->kind == "await using") {
        diag_.ReportError(vd->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript using declarations require deterministic-disposal "
                          "runtime lowering");
        return;
      }
      for (auto &d : vd->decls) {
        if (!d.name.empty() && (d.name.front() == '[' || d.name.front() == '{')) {
          diag_.ReportError(vd->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "JavaScript destructuring declarations require iterator/property "
                            "runtime semantics");
          continue;
        }
        if (!d.init) {
          diag_.ReportError(vd->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "JavaScript uninitialized declarations require a distinct undefined "
                            "value representation");
          continue;
        }
        ir::IRType ty = d.type ? ToIRType(d.type) : InferExpressionType(d.init);
        if (ty.kind == ir::IRTypeKind::kInvalid || ty.kind == ir::IRTypeKind::kVoid) {
          diag_.ReportError(vd->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "JavaScript declaration type cannot be represented faithfully in IR");
          continue;
        }
        auto a = builder_.MakeAlloca(ty, d.name + ".addr");
        locals_[d.name] = {d.name + ".addr", ty};
        auto v = LowerExpression(d.init, ty);
        if (!v.empty())
          builder_.MakeStore(d.name + ".addr", v);
      }
      return;
    }
    if (auto es = std::dynamic_pointer_cast<ExprStatement>(s)) {
      LowerExpression(es->expr, ir::IRType::F64());
      return;
    }
    if (auto rs = std::dynamic_pointer_cast<ReturnStatement>(s)) {
      if (rs->value) {
        if (current_ret_.kind == ir::IRTypeKind::kVoid) {
          diag_.ReportError(rs->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "JavaScript value return conflicts with the declared void ABI");
          builder_.MakeUnreachable();
          terminated_ = true;
          return;
        }
        auto actual_type = InferExpressionType(rs->value);
        if (actual_type.kind == ir::IRTypeKind::kInvalid ||
            !actual_type.SameShape(current_ret_)) {
          diag_.ReportError(rs->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "JavaScript return value requires an unmodeled conversion to the "
                            "declared JSDoc return type");
          builder_.MakeUnreachable();
          terminated_ = true;
          return;
        }
        auto v = LowerExpression(rs->value, current_ret_);
        builder_.MakeReturn(v);
      } else {
        if (current_ret_.kind != ir::IRTypeKind::kVoid) {
          diag_.ReportError(rs->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "JavaScript bare return requires an undefined value representation");
          builder_.MakeUnreachable();
        } else {
          builder_.MakeReturn();
        }
      }
      terminated_ = true;
      return;
    }
    if (auto ifs = std::dynamic_pointer_cast<IfStatement>(s)) {
      if (!RequireBoolean(ifs->condition, "if"))
        return;
      auto fn = builder_.CurrentFunction();
      auto then_bb = fn->CreateBlock("if.then");
      auto else_bb = ifs->else_branch ? fn->CreateBlock("if.else") : nullptr;
      auto cont_bb = fn->CreateBlock("if.end");
      auto cond = LowerExpression(ifs->condition, ir::IRType::I1());
      builder_.MakeCondBranch(cond, then_bb, else_bb ? else_bb : cont_bb);

      builder_.SetInsertPoint(then_bb);
      terminated_ = false;
      LowerStatement(ifs->then_branch);
      if (!terminated_)
        builder_.MakeBranch(cont_bb);

      if (else_bb) {
        builder_.SetInsertPoint(else_bb);
        terminated_ = false;
        LowerStatement(ifs->else_branch);
        if (!terminated_)
          builder_.MakeBranch(cont_bb);
      }
      builder_.SetInsertPoint(cont_bb);
      terminated_ = false;
      return;
    }
    if (auto wh = std::dynamic_pointer_cast<WhileStatement>(s)) {
      if (!RequireBoolean(wh->condition, "while"))
        return;
      auto fn = builder_.CurrentFunction();
      auto cond_bb = fn->CreateBlock("while.cond");
      auto body_bb = fn->CreateBlock("while.body");
      auto end_bb = fn->CreateBlock("while.end");
      builder_.MakeBranch(cond_bb);
      builder_.SetInsertPoint(cond_bb);
      auto cv = LowerExpression(wh->condition, ir::IRType::I1());
      builder_.MakeCondBranch(cv, body_bb, end_bb);
      builder_.SetInsertPoint(body_bb);
      terminated_ = false;
      loop_stack_.push_back({cond_bb, end_bb});
      LowerStatement(wh->body);
      loop_stack_.pop_back();
      if (!terminated_)
        builder_.MakeBranch(cond_bb);
      builder_.SetInsertPoint(end_bb);
      terminated_ = false;
      return;
    }
    if (auto fs = std::dynamic_pointer_cast<ForStatement>(s)) {
      if (fs->condition && !RequireBoolean(fs->condition, "for"))
        return;
      auto fn = builder_.CurrentFunction();
      if (fs->init)
        LowerStatement(fs->init);
      auto cond_bb = fn->CreateBlock("for.cond");
      auto body_bb = fn->CreateBlock("for.body");
      auto step_bb = fn->CreateBlock("for.step");
      auto end_bb = fn->CreateBlock("for.end");
      builder_.MakeBranch(cond_bb);
      builder_.SetInsertPoint(cond_bb);
      if (fs->condition) {
        auto cv = LowerExpression(fs->condition, ir::IRType::I1());
        builder_.MakeCondBranch(cv, body_bb, end_bb);
      } else {
        builder_.MakeBranch(body_bb);
      }
      builder_.SetInsertPoint(body_bb);
      terminated_ = false;
      loop_stack_.push_back({step_bb, end_bb});
      LowerStatement(fs->body);
      loop_stack_.pop_back();
      if (!terminated_)
        builder_.MakeBranch(step_bb);
      builder_.SetInsertPoint(step_bb);
      terminated_ = false;
      if (fs->update)
        LowerExpression(fs->update, ir::IRType::F64());
      builder_.MakeBranch(cond_bb);
      builder_.SetInsertPoint(end_bb);
      terminated_ = false;
      return;
    }
    if (auto br = std::dynamic_pointer_cast<BreakStatement>(s)) {
      if (!br->label.empty()) {
        diag_.ReportError(br->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "labeled JavaScript break requires label-aware CFG lowering");
      } else if (!loop_stack_.empty()) {
        builder_.MakeBranch(loop_stack_.back().brk);
        terminated_ = true;
      } else {
        diag_.ReportError(br->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript break outside a modeled loop has no CFG target");
      }
      return;
    }
    if (auto co = std::dynamic_pointer_cast<ContinueStatement>(s)) {
      if (!co->label.empty()) {
        diag_.ReportError(co->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "labeled JavaScript continue requires label-aware CFG lowering");
      } else if (!loop_stack_.empty()) {
        builder_.MakeBranch(loop_stack_.back().cont);
        terminated_ = true;
      } else {
        diag_.ReportError(co->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript continue outside a modeled loop has no CFG target");
      }
      return;
    }
    diag_.ReportError(s->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "JavaScript statement is parsed but has no faithful IR lowering");
  }

  // ---- Expression lowering ----------------------------------------------

  static bool IsFloat(const ir::IRType &t) {
    return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
  }

  static bool IsInteger(const ir::IRType &t) { return t.IsInteger() && t.kind != ir::IRTypeKind::kI1; }

  ir::IRType InferExpressionType(const std::shared_ptr<Expression> &e) const {
    if (!e)
      return ir::IRType::Invalid();
    if (auto lit = std::dynamic_pointer_cast<Literal>(e)) {
      switch (lit->kind) {
      case Literal::Kind::kNumber: return ir::IRType::F64();
      case Literal::Kind::kBool: return ir::IRType::I1();
      case Literal::Kind::kString:
      case Literal::Kind::kTemplateString: return ir::IRType::Pointer(ir::IRType::I8());
      default: return ir::IRType::Invalid();
      }
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
      auto it = locals_.find(id->name);
      return it == locals_.end() ? ir::IRType::Invalid() : it->second.type;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
      if (bin->op == "==" || bin->op == "!=")
        return ir::IRType::Invalid();
      auto lhs = InferExpressionType(bin->left);
      auto rhs = InferExpressionType(bin->right);
      if (lhs.kind == ir::IRTypeKind::kInvalid || !lhs.SameShape(rhs))
        return ir::IRType::Invalid();
      if (bin->op == "===" || bin->op == "!==" || bin->op == "<" || bin->op == "<=" ||
          bin->op == ">" || bin->op == ">=")
        return lhs.IsScalar() ? ir::IRType::I1() : ir::IRType::Invalid();
      if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" ||
          bin->op == "%")
        return (IsFloat(lhs) || IsInteger(lhs)) ? lhs : ir::IRType::Invalid();
      if (bin->op == "&" || bin->op == "|" || bin->op == "^" || bin->op == "<<" ||
          bin->op == ">>" || bin->op == ">>>")
        return IsInteger(lhs) ? lhs : ir::IRType::Invalid();
      return ir::IRType::Invalid();
    }
    if (auto logical = std::dynamic_pointer_cast<LogicalExpr>(e)) {
      // With boolean operands JavaScript's operand-valued && / || agrees
      // exactly with a boolean merge. Dynamic truthiness remains explicit.
      if ((logical->op == "&&" || logical->op == "||") &&
          InferExpressionType(logical->left).kind == ir::IRTypeKind::kI1 &&
          InferExpressionType(logical->right).kind == ir::IRTypeKind::kI1)
        return ir::IRType::I1();
      return ir::IRType::Invalid();
    }
    if (auto unary = std::dynamic_pointer_cast<UnaryExpr>(e)) {
      auto operand = InferExpressionType(unary->operand);
      if (unary->op == "!")
        return operand.kind == ir::IRTypeKind::kI1 ? ir::IRType::I1() : ir::IRType::Invalid();
      if (unary->op == "+" || unary->op == "-")
        return (IsFloat(operand) || IsInteger(operand)) ? operand : ir::IRType::Invalid();
      if (unary->op == "~")
        return IsInteger(operand) ? operand : ir::IRType::Invalid();
      return ir::IRType::Invalid();
    }
    if (auto update = std::dynamic_pointer_cast<UpdateExpr>(e)) {
      auto id = std::dynamic_pointer_cast<Identifier>(update->target);
      if (!id)
        return ir::IRType::Invalid();
      auto it = locals_.find(id->name);
      if (it == locals_.end() || (!IsFloat(it->second.type) && !IsInteger(it->second.type)))
        return ir::IRType::Invalid();
      return it->second.type;
    }
    if (auto assign = std::dynamic_pointer_cast<AssignExpr>(e)) {
      if (assign->op != "=")
        return ir::IRType::Invalid();
      auto id = std::dynamic_pointer_cast<Identifier>(assign->target);
      if (!id)
        return ir::IRType::Invalid();
      auto it = locals_.find(id->name);
      auto rhs = InferExpressionType(assign->value);
      if (it == locals_.end() || !it->second.type.SameShape(rhs))
        return ir::IRType::Invalid();
      return it->second.type;
    }
    if (auto conditional = std::dynamic_pointer_cast<ConditionalExpr>(e)) {
      if (InferExpressionType(conditional->test).kind != ir::IRTypeKind::kI1)
        return ir::IRType::Invalid();
      auto then_type = InferExpressionType(conditional->then_branch);
      auto else_type = InferExpressionType(conditional->else_branch);
      return then_type.SameShape(else_type) ? then_type : ir::IRType::Invalid();
    }
    if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
      auto id = std::dynamic_pointer_cast<Identifier>(call->callee);
      if (!id)
        return ir::IRType::Invalid();
      if (const auto *api = frontends::FindNativeBuiltin(id->name))
        return frontends::NativeIRType(api->result);
      auto it = function_signatures_.find(id->name);
      return it == function_signatures_.end() ? ir::IRType::Invalid() : it->second.return_type;
    }
    if (std::dynamic_pointer_cast<TemplateLiteral>(e))
      return ir::IRType::Pointer(ir::IRType::I8());
    return ir::IRType::Invalid();
  }

  bool RequireBoolean(const std::shared_ptr<Expression> &expression, const std::string &context) {
    if (InferExpressionType(expression).kind == ir::IRTypeKind::kI1)
      return true;
    diag_.ReportError(expression->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "JavaScript " + context +
                          " condition requires ToBoolean/dynamic truthiness lowering");
    return false;
  }

  std::string LowerExpression(const std::shared_ptr<Expression> &e, const ir::IRType &want) {
    if (!e)
      return "";
    if (auto lit = std::dynamic_pointer_cast<Literal>(e)) {
      return LowerLiteral(*lit, want);
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
      auto it = locals_.find(id->name);
      if (it != locals_.end()) {
        auto load = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
        return load->name;
      }
      diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "unresolved JavaScript identifier '" + id->name +
                            "' requires dynamic environment lookup");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
      return LowerBinary(*bin, want);
    }
    if (auto logical = std::dynamic_pointer_cast<LogicalExpr>(e)) {
      if (InferExpressionType(e).kind != ir::IRTypeKind::kI1) {
        diag_.ReportError(logical->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "JavaScript logical expressions currently require boolean operands; "
                         "dynamic truthiness and nullish values need tagged runtime semantics");
        return builder_.MakeLiteral((long long)0)->name;
      }
      const auto left = LowerExpression(logical->left, ir::IRType::I1());
      auto *left_block = builder_.GetInsertPoint().get();
      auto *right_block = builder_.CurrentFunction()->CreateBlock("logical.rhs");
      auto *merge_block = builder_.CurrentFunction()->CreateBlock("logical.end");
      if (logical->op == "&&") builder_.MakeCondBranch(left, right_block, merge_block);
      else builder_.MakeCondBranch(left, merge_block, right_block);
      builder_.SetInsertPoint(right_block);
      const auto right = LowerExpression(logical->right, ir::IRType::I1());
      auto *right_end = builder_.GetInsertPoint().get();
      builder_.MakeBranch(merge_block);
      builder_.SetInsertPoint(merge_block);
      return builder_.MakePhi(ir::IRType::I1(), {{left_block, left}, {right_end, right}},
                              "logical.value")->name;
    }
    if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
      return LowerUnary(*u, want);
    }
    if (auto u = std::dynamic_pointer_cast<UpdateExpr>(e)) {
      return LowerUpdate(*u);
    }
    if (auto a = std::dynamic_pointer_cast<AssignExpr>(e)) {
      if (a->op != "=") {
        diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript compound/logical assignment operator '" + a->op +
                              "' requires read-modify-write and short-circuit semantics");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      if (auto id = std::dynamic_pointer_cast<Identifier>(a->target)) {
        auto it = locals_.find(id->name);
        if (it != locals_.end()) {
          auto rhs_type = InferExpressionType(a->value);
          if (!it->second.type.SameShape(rhs_type)) {
            diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                              "JavaScript assignment requires an unmodeled value conversion");
            return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                                 : builder_.MakeLiteral((long long)0)->name;
          }
          auto rhs = LowerExpression(a->value, it->second.type);
          builder_.MakeStore(it->second.addr, rhs);
          return rhs;
        } else {
          diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "assignment to unresolved JavaScript binding requires dynamic "
                            "environment lookup");
        }
      } else {
        diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript destructuring/member assignment requires runtime lowering");
      }
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    if (auto c = std::dynamic_pointer_cast<ConditionalExpr>(e)) {
      if (!RequireBoolean(c->test, "conditional"))
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      auto result_type = InferExpressionType(e);
      if (result_type.kind == ir::IRTypeKind::kInvalid || !result_type.SameShape(want)) {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript conditional branches need a common modeled IR type");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      auto fn = builder_.CurrentFunction();
      auto then_bb = fn->CreateBlock("cond.then");
      auto else_bb = fn->CreateBlock("cond.else");
      auto end_bb = fn->CreateBlock("cond.end");
      auto cv = LowerExpression(c->test, ir::IRType::I1());
      auto slot = builder_.MakeAlloca(want, "cond.tmp");
      builder_.MakeCondBranch(cv, then_bb, else_bb);
      builder_.SetInsertPoint(then_bb);
      auto t = LowerExpression(c->then_branch, want);
      builder_.MakeStore(slot->name, t);
      builder_.MakeBranch(end_bb);
      builder_.SetInsertPoint(else_bb);
      auto f = LowerExpression(c->else_branch, want);
      builder_.MakeStore(slot->name, f);
      builder_.MakeBranch(end_bb);
      builder_.SetInsertPoint(end_bb);
      auto load = builder_.MakeLoad(slot->name, want, "cond.val");
      return load->name;
    }
    if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
      return LowerCall(*call, want);
    }
    if (auto templ = std::dynamic_pointer_cast<TemplateLiteral>(e)) {
      std::vector<std::string> parts;
      for (size_t i = 0; i < templ->quasis.size(); ++i) {
        parts.push_back(builder_.MakeStringLiteral(templ->quasis[i], "tmpl.quasi"));
        if (i < templ->expressions.size() && templ->expressions[i]) {
          auto value = LowerExpression(templ->expressions[i], ir::IRType::F64());
          auto string_value = builder_.MakeCall("__js_to_string", {value},
                                                ir::IRType::Pointer(ir::IRType::I8()),
                                                "tmpl.value");
          parts.push_back(string_value->name);
        }
      }
      auto joined = builder_.MakeCall("__js_string_concat", parts,
                                      ir::IRType::Pointer(ir::IRType::I8()), "template");
      return joined->name;
    }
    // Keep the IR structurally valid after reporting the unsupported node;
    // callers must reject the result because diagnostics now contains E4003.
    diag_.ReportError(e->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "JavaScript expression is parsed but has no faithful IR lowering");
    return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                         : builder_.MakeLiteral((long long)0)->name;
  }

  std::string LowerLiteral(const Literal &lit, const ir::IRType &want) {
    switch (lit.kind) {
    case Literal::Kind::kNumber: {
      std::string normalized;
      normalized.reserve(lit.value.size());
      for (char ch : lit.value) {
        if (ch != '_')
          normalized.push_back(ch);
      }
      try {
        long long integer_value = 0;
        bool integer_base_literal = normalized.size() > 2 && normalized[0] == '0' &&
                                    (normalized[1] == 'x' || normalized[1] == 'X' ||
                                     normalized[1] == 'b' || normalized[1] == 'B' ||
                                     normalized[1] == 'o' || normalized[1] == 'O');
        if (integer_base_literal) {
          int base = (normalized[1] == 'x' || normalized[1] == 'X')
                         ? 16
                         : ((normalized[1] == 'b' || normalized[1] == 'B') ? 2 : 8);
          size_t consumed = 0;
          integer_value = std::stoll(normalized.substr(2), &consumed, base);
          if (consumed != normalized.size() - 2)
            throw std::invalid_argument("trailing numeric characters");
          if (IsFloat(want))
            return builder_.MakeLiteral(static_cast<double>(integer_value))->name;
        } else {
          size_t consumed = 0;
          if (IsFloat(want)) {
            double value = std::stod(normalized, &consumed);
            if (consumed != normalized.size())
              throw std::invalid_argument("trailing numeric characters");
            return builder_.MakeLiteral(value)->name;
          }
          integer_value = std::stoll(normalized, &consumed, 10);
          if (consumed != normalized.size())
            throw std::invalid_argument("trailing numeric characters");
        }
        return builder_.MakeLiteral(integer_value)->name;
      } catch (...) {
        diag_.ReportError(lit.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript numeric literal cannot be represented by the declared IR type");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
    }
    case Literal::Kind::kBigInt: {
      diag_.ReportError(lit.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript BigInt requires arbitrary-precision runtime lowering");
      return builder_.MakeLiteral((long long)0)->name;
    }
    case Literal::Kind::kBool: {
      auto value = builder_.MakeLiteral((long long)(lit.value == "true" ? 1 : 0))->name;
      auto one = builder_.MakeLiteral((long long)1)->name;
      return builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, value, one, "bool")->name;
    }
    case Literal::Kind::kNull:
    case Literal::Kind::kUndefined:
      diag_.ReportError(lit.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript null/undefined require distinct tagged runtime values");
      return builder_.MakeLiteral((long long)0)->name;
    case Literal::Kind::kString:
    case Literal::Kind::kTemplateString:
      return builder_.MakeStringLiteral(lit.value, "jsstr");
    case Literal::Kind::kRegex:
      diag_.ReportError(lit.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript RegExp literals require RegExp-object runtime lowering");
      return builder_.MakeLiteral((long long)0)->name;
    }
    diag_.ReportError(lit.loc, frontends::ErrorCode::kUnsupportedLowering,
                      "JavaScript literal kind has no faithful IR representation");
    return builder_.MakeLiteral((long long)0)->name;
  }

  std::string LowerBinary(const BinaryExpr &b, const ir::IRType &want) {
    if (b.op == "==" || b.op == "!=") {
      diag_.ReportError(b.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript loose equality requires coercion and abstract-equality semantics");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    auto operand_type = InferExpressionType(b.left);
    auto rhs_type = InferExpressionType(b.right);
    if (operand_type.kind == ir::IRTypeKind::kInvalid || !operand_type.SameShape(rhs_type)) {
      diag_.ReportError(b.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript binary operands require unmodeled coercion or dynamic dispatch");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    const bool numeric = IsFloat(operand_type) || IsInteger(operand_type);
    const bool bitwise = b.op == "&" || b.op == "|" || b.op == "^" || b.op == "<<" ||
                         b.op == ">>" || b.op == ">>>";
    if ((!numeric && b.op != "===" && b.op != "!==") ||
        ((b.op == "===" || b.op == "!==") && !operand_type.IsScalar()) ||
        (bitwise && operand_type.kind != ir::IRTypeKind::kI32) ||
        ((b.op == "/" || b.op == "%") && !IsFloat(operand_type))) {
      diag_.ReportError(b.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript operator '" + b.op +
                            "' needs ToNumber/ToInt32/string runtime semantics");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    auto l = LowerExpression(b.left, operand_type);
    auto r = LowerExpression(b.right, operand_type);
    using Op = ir::BinaryInstruction::Op;
    Op op;
    bool is_cmp = false;
    bool is_float = IsFloat(operand_type);
    if (b.op == "+")
      op = is_float ? Op::kFAdd : Op::kAdd;
    else if (b.op == "-")
      op = is_float ? Op::kFSub : Op::kSub;
    else if (b.op == "*")
      op = is_float ? Op::kFMul : Op::kMul;
    else if (b.op == "/")
      op = is_float ? Op::kFDiv : Op::kSDiv;
    else if (b.op == "%")
      op = is_float ? Op::kFRem : Op::kSRem;
    else if (b.op == "&")
      op = Op::kAnd;
    else if (b.op == "|")
      op = Op::kOr;
    else if (b.op == "^")
      op = Op::kXor;
    else if (b.op == "<<")
      op = Op::kShl;
    else if (b.op == ">>")
      op = Op::kAShr;
    else if (b.op == ">>>")
      op = Op::kLShr;
    else if (b.op == "===") {
      op = is_float ? Op::kCmpFoe : Op::kCmpEq;
      is_cmp = true;
    } else if (b.op == "!==") {
      op = is_float ? Op::kCmpFne : Op::kCmpNe;
      is_cmp = true;
    } else if (b.op == "<") {
      op = is_float ? Op::kCmpFlt
                    : (operand_type.is_signed ? Op::kCmpSlt : Op::kCmpUlt);
      is_cmp = true;
    } else if (b.op == "<=") {
      op = is_float ? Op::kCmpFle
                    : (operand_type.is_signed ? Op::kCmpSle : Op::kCmpUle);
      is_cmp = true;
    } else if (b.op == ">") {
      op = is_float ? Op::kCmpFgt
                    : (operand_type.is_signed ? Op::kCmpSgt : Op::kCmpUgt);
      is_cmp = true;
    } else if (b.op == ">=") {
      op = is_float ? Op::kCmpFge
                    : (operand_type.is_signed ? Op::kCmpSge : Op::kCmpUge);
      is_cmp = true;
    } else {
      diag_.ReportError(b.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript binary operator '" + b.op +
                            "' has no faithful static IR lowering");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    auto bi = builder_.MakeBinary(op, l, r, is_cmp ? "cmp" : "bop");
    return bi->name;
  }

  std::string LowerUnary(const UnaryExpr &u, const ir::IRType &want) {
    auto operand_type = InferExpressionType(u.operand);
    if (operand_type.kind == ir::IRTypeKind::kInvalid) {
      diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript unary operand requires dynamic conversion semantics");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    auto v = LowerExpression(u.operand, operand_type);
    if (u.op == "-") {
      if (!IsFloat(operand_type) && !IsInteger(operand_type)) {
        diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript unary minus requires ToNumber runtime semantics");
        return builder_.MakeLiteral((long long)0)->name;
      }
      if (IsFloat(operand_type))
        return builder_.MakeFloatNegate(v, operand_type, "neg")->name;
      auto zero = builder_.MakeLiteral((long long)0)->name;
      auto bi = builder_.MakeBinary(ir::BinaryInstruction::Op::kSub, zero, v, "neg");
      return bi->name;
    }
    if (u.op == "!") {
      if (operand_type.kind != ir::IRTypeKind::kI1) {
        diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript logical not requires ToBoolean runtime semantics");
        return builder_.MakeLiteral((long long)0)->name;
      }
      auto one = builder_.MakeLiteral((long long)1)->name;
      auto bi = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, v, one, "not");
      return bi->name;
    }
    if (u.op == "~") {
      if (!IsInteger(operand_type) || operand_type.kind != ir::IRTypeKind::kI32) {
        diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript bitwise not requires modeled ToInt32 semantics");
        return builder_.MakeLiteral((long long)0)->name;
      }
      auto m1 = builder_.MakeLiteral((long long)-1)->name;
      auto bi = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, v, m1, "bnot");
      return bi->name;
    }
    if (u.op == "+" && (IsFloat(operand_type) || IsInteger(operand_type)))
      return v;
    diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                      "JavaScript unary operator '" + u.op +
                          "' requires runtime semantics");
    return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                         : builder_.MakeLiteral((long long)0)->name;
  }

  std::string LowerUpdate(const UpdateExpr &u) {
    if (u.op != "++" && u.op != "--") {
      diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "unknown JavaScript update operator '" + u.op + "'");
      return "";
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(u.target)) {
      auto it = locals_.find(id->name);
      if (it == locals_.end()) {
        diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "update of unresolved JavaScript binding requires dynamic lookup");
        return "";
      }
      if (!IsFloat(it->second.type) && !IsInteger(it->second.type)) {
        diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript update requires ToNumeric runtime semantics");
        return "";
      }
      auto loaded = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
      auto one = IsFloat(it->second.type) ? builder_.MakeLiteral(1.0)->name
                                          : builder_.MakeLiteral((long long)1)->name;
      auto op = (u.op == "++") ? (IsFloat(it->second.type) ? ir::BinaryInstruction::Op::kFAdd
                                                           : ir::BinaryInstruction::Op::kAdd)
                               : (IsFloat(it->second.type) ? ir::BinaryInstruction::Op::kFSub
                                                           : ir::BinaryInstruction::Op::kSub);
      auto bi = builder_.MakeBinary(op, loaded->name, one, "upd");
      builder_.MakeStore(it->second.addr, bi->name);
      return u.prefix ? bi->name : loaded->name;
    }
    diag_.ReportError(u.loc, frontends::ErrorCode::kUnsupportedLowering,
                      "JavaScript member update requires runtime lowering");
    return "";
  }

  static std::optional<long long> SafeIntegerLiteral(const std::shared_ptr<Expression> &expression) {
    if (auto unary = std::dynamic_pointer_cast<UnaryExpr>(expression)) {
      if (unary->op != "+" && unary->op != "-") return std::nullopt;
      auto value = SafeIntegerLiteral(unary->operand);
      if (!value) return std::nullopt;
      return unary->op == "-" ? -*value : *value;
    }
    auto literal = std::dynamic_pointer_cast<Literal>(expression);
    if (!literal || literal->kind != Literal::Kind::kNumber) return std::nullopt;
    std::string text;
    for (char c : literal->value) if (c != '_') text += c;
    constexpr long long max_safe_integer = 9007199254740991LL;
    try {
      std::size_t consumed = 0;
      if (text.size() > 2 && text[0] == '0' &&
          (text[1] == 'x' || text[1] == 'X' || text[1] == 'b' || text[1] == 'B' ||
           text[1] == 'o' || text[1] == 'O')) {
        const int base = text[1] == 'x' || text[1] == 'X' ? 16 :
                         text[1] == 'b' || text[1] == 'B' ? 2 : 8;
        const auto value = std::stoll(text.substr(2), &consumed, base);
        if (consumed == text.size() - 2 && value >= 0 && value <= max_safe_integer) return value;
        return std::nullopt;
      }
      const double value = std::stod(text, &consumed);
      if (consumed == text.size() && std::isfinite(value) &&
          std::trunc(value) == value && std::abs(value) <= static_cast<double>(max_safe_integer))
        return static_cast<long long>(value);
    } catch (...) {
    }
    return std::nullopt;
  }

  std::string LowerCall(const CallExpr &c, const ir::IRType &want) {
    if (c.optional || c.is_new) {
      diag_.ReportError(c.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript optional/new calls require dynamic runtime lowering");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    std::string callee_name;
    if (auto id = std::dynamic_pointer_cast<Identifier>(c.callee)) {
      callee_name = id->name;
    } else if (auto m = std::dynamic_pointer_cast<MemberExpr>(c.callee)) {
      diag_.ReportError(m->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript method calls require receiver/dynamic-dispatch lowering");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    } else {
      diag_.ReportError(c.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript computed call target requires dynamic runtime lowering");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }

    if (const auto *api = frontends::FindNativeBuiltin(callee_name)) {
      std::vector<std::string> values;
      std::vector<ir::IRType> types;
      for (std::size_t i = 0; i < c.args.size(); ++i) {
        const auto &arg = c.args[i];
        auto type = InferExpressionType(arg);
        if (i < api->params.size() && api->params[i] == frontends::NativeType::kString) {
          if (std::dynamic_pointer_cast<TemplateLiteral>(arg)) {
            diag_.ReportError(arg->loc, frontends::ErrorCode::kUnsupportedLowering,
                "JavaScript native string arguments do not support template interpolation");
            return "";
          }
          if (auto literal = std::dynamic_pointer_cast<Literal>(arg);
              literal && literal->kind == Literal::Kind::kString) {
            const auto &source = literal->value;
            std::string normalized, decoded, error;
            bool valid = source.size() >= 2 && (source.front() == '\'' || source.front() == '"') &&
                         source.back() == source.front();
            // The shared decoder accepts C escapes. Limit this API boundary to
            // JavaScript escapes and normalize its fixed-width hexadecimal form.
            for (std::size_t p = 1; valid && p + 1 < source.size(); ++p) {
              if (source[p] != '\\') { normalized.push_back(source[p]); continue; }
              if (++p + 1 >= source.size()) { valid = false; break; }
              const char escape = source[p];
              if (escape == 'x') {
                if (p + 3 >= source.size() || !std::isxdigit(static_cast<unsigned char>(source[p + 1])) ||
                    !std::isxdigit(static_cast<unsigned char>(source[p + 2]))) {
                  valid = false; break;
                }
                normalized += "\\u00";
                normalized.append(source, p + 1, 2);
                p += 2;
              } else if (std::string("nrtbfv\\\"'u").find(escape) != std::string::npos) {
                normalized.push_back('\\'); normalized.push_back(escape);
              } else {
                valid = false;
              }
            }
            if (!valid || !frontends::DecodeNativeString(normalized, false, false, decoded, error)) {
              diag_.ReportError(arg->loc, frontends::ErrorCode::kUnsupportedLowering,
                  "JavaScript native string literal: " + (valid ? error : "unsupported escape or quoted form"));
              return "";
            }
            types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
            values.push_back(builder_.MakeStringLiteral(decoded, "jsnative"));
            continue;
          }
        }
        if (i < api->params.size() && api->params[i] == frontends::NativeType::kInt &&
            type.kind == ir::IRTypeKind::kF64) {
          if (auto exact = SafeIntegerLiteral(arg)) {
            types.push_back(ir::IRType::I64(true));
            values.push_back(builder_.MakeLiteral(*exact)->name);
            continue;
          }
          diag_.ReportError(arg->loc, frontends::ErrorCode::kUnsupportedLowering,
              "JavaScript native integer argument requires an integer ABI value or a safe "
              "integer Number literal; Number variables are not converted implicitly");
          return "";
        }
        types.push_back(type); values.push_back(LowerExpression(arg, type));
      }
      auto inst = frontends::EmitNativeBuiltin(*api, values, types, builder_, ctx_, diag_, c.loc);
      return inst ? inst->name : "";
    }
    auto signature = function_signatures_.find(callee_name);
    if (signature == function_signatures_.end() ||
        signature->second.return_type.kind == ir::IRTypeKind::kInvalid) {
      diag_.ReportError(c.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript call target '" + callee_name +
                            "' has no statically modeled JSDoc signature");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    if (c.args.size() != signature->second.param_types.size()) {
      diag_.ReportError(c.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "JavaScript call arity/default binding is not represented by the IR ABI");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }

    std::vector<std::string> args;
    args.reserve(c.args.size());
    for (size_t i = 0; i < c.args.size(); ++i) {
      const auto &param_type = signature->second.param_types[i];
      auto arg_type = InferExpressionType(c.args[i]);
      if (param_type.kind == ir::IRTypeKind::kInvalid || !param_type.SameShape(arg_type)) {
        diag_.ReportError(c.args[i]->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "JavaScript call argument requires an unmodeled conversion");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      args.push_back(LowerExpression(c.args[i], param_type));
    }
    auto call = builder_.MakeCall(callee_name, args, signature->second.return_type, "call");
    return call->name;
  }

  struct LoopFrame {
    ir::BasicBlock *cont;
    ir::BasicBlock *brk;
  };
  struct Local {
    std::string addr;
    ir::IRType type;
  };

  const Module &module_;
  ir::IRContext &ctx_;
  ir::IRBuilder builder_;
  frontends::Diagnostics &diag_;
  std::unordered_map<std::string, Local> locals_;
  std::unordered_map<std::string, FunctionSignature> function_signatures_;
  std::vector<LoopFrame> loop_stack_;
  ir::IRType current_ret_{ir::IRType::Void()};
  bool terminated_{false};
};

} // namespace

void LowerToIR(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag) {
  Lowerer l(mod, ctx, diag);
  l.Run();
}

} // namespace polyglot::javascript
