#include "frontends/common/include/native_builtins.h"
#include "frontends/common/include/native_string_literal.h"
/**
 * @file     lowering.cpp
 * @brief    Ruby - Polyglot IR lowering
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Lowers a typed numeric subset of Ruby to IR.  Methods declared with
 * YARD `@param`/`@return` tags participate in the cross-language IR
 * surface; everything else is reported but not embedded.
 */
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/ir_builder.h"

#include "frontends/ruby/include/ruby_lowering.h"

namespace polyglot::ruby {

namespace {

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &t) {
  if (!t)
    return ir::IRType::Invalid();
  const std::string &n = t->name;
  if (n == "Integer" || n == "Fixnum" || n == "Bignum" || n == "Numeric")
    return ir::IRType::I64();
  if (n == "Float")
    return ir::IRType::F64();
  if (n == "String" || n == "Symbol")
    return ir::IRType::Pointer(ir::IRType::I8());
  if (n == "TrueClass" || n == "FalseClass" || n == "Boolean")
    return ir::IRType::I1();
  if (n == "NilClass" || n == "Nil")
    return ir::IRType::Void();
  if (n == "Array" || n == "Hash" || n == "Object")
    return ir::IRType::Pointer(ir::IRType::I8());
  return ir::IRType::Invalid();
}

class Lowerer {
public:
  Lowerer(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag) :
      module_(mod), ctx_(ctx), builder_(ctx), diag_(diag) {}

  void Run() {
    // Only local, explicitly typed top-level methods use static dispatch.
    for (const auto &s : module_.body) {
      if (auto m = std::dynamic_pointer_cast<MethodDecl>(s)) signatures_[m->name] = m.get();
    }
    for (auto &s : module_.body)
      Top(s, "");
  }

private:
  void Top(const std::shared_ptr<Statement> &s, const std::string &prefix) {
    if (auto m = std::dynamic_pointer_cast<MethodDecl>(s)) {
      LowerMethod(*m, prefix);
    } else if (auto c = std::dynamic_pointer_cast<ClassDecl>(s)) {
      std::string p = prefix.empty() ? c->name : prefix + "::" + c->name;
      for (auto &b : c->body)
        Top(b, p);
    } else if (auto md = std::dynamic_pointer_cast<ModuleDecl>(s)) {
      std::string p = prefix.empty() ? md->name : prefix + "::" + md->name;
      for (auto &b : md->body)
        Top(b, p);
    } else if (s) {
      diag_.ReportError(s->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Ruby top-level executable statement has no module-initializer lowering");
    }
  }

  void LowerMethod(const MethodDecl &m, const std::string &prefix) {
    std::vector<std::pair<std::string, ir::IRType>> ir_params;
    for (auto &p : m.params) {
      if (p.forwarding || p.splat || p.double_splat || p.block || p.default_value) {
        diag_.ReportError(
            m.loc, frontends::ErrorCode::kUnsupportedLowering,
            "Ruby forwarding/splat/block/default parameters require dynamic call-protocol "
            "lowering");
        return;
      }
      auto param_type = ToIRType(p.type);
      if (param_type.kind == ir::IRTypeKind::kInvalid) {
        diag_.ReportError(m.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby parameter '" + p.name +
                              "' requires a supported YARD type for static lowering");
        return;
      }
      ir_params.emplace_back(p.name, param_type);
    }
    auto ret = ToIRType(m.return_type);
    if (ret.kind == ir::IRTypeKind::kInvalid) {
      diag_.ReportError(m.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Ruby method '" + m.name +
                            "' requires a supported YARD return type for static lowering");
      return;
    }
    std::string name = prefix.empty() ? m.name : prefix + "::" + m.name;
    auto fn = ctx_.CreateFunction(name, ret, ir_params);
    builder_.SetCurrentFunction(fn);
    auto entry = builder_.CreateBlock("entry");
    builder_.SetInsertPoint(entry);
    locals_.clear();
    for (auto &p : ir_params) {
      builder_.MakeAlloca(p.second, p.first + ".addr");
      builder_.MakeStore(p.first + ".addr", p.first);
      locals_[p.first] = {p.first + ".addr", p.second};
    }
    current_ret_ = ret;
    terminated_ = false;
    last_expr_type_ = ir::IRType::Invalid();
    std::string last_value;
    LowerStmt(m.body, &last_value);
    if (!terminated_) {
      // Ruby returns the value of the last expression.
      if (ret.kind == ir::IRTypeKind::kVoid) {
        builder_.MakeReturn();
      } else if (!last_value.empty() && last_expr_type_.SameShape(ret)) {
        builder_.MakeReturn(last_value);
      } else {
        diag_.ReportError(m.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby non-void method has no faithfully lowered final expression");
        builder_.MakeUnreachable();
      }
    }
    builder_.ClearCurrentFunction();
  }

  void LowerStmt(const std::shared_ptr<Statement> &s, std::string *last_val) {
    if (!s || terminated_)
      return;
    if (auto blk = std::dynamic_pointer_cast<Block>(s)) {
      for (auto &c : blk->stmts) {
        if (terminated_)
          return;
        LowerStmt(c, last_val);
      }
      return;
    }
    if (auto es = std::dynamic_pointer_cast<ExprStmt>(s)) {
      auto expression_type = InferExprType(es->expr);
      if (expression_type.kind == ir::IRTypeKind::kInvalid) {
        diag_.ReportError(es->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby expression result has no modeled static type");
        return;
      }
      auto v = LowerExpr(es->expr, expression_type);
      if (last_val) {
        *last_val = v;
        last_expr_type_ = expression_type;
      }
      return;
    }
    if (auto rs = std::dynamic_pointer_cast<ReturnStmt>(s)) {
      std::string v;
      if (rs->value) {
        if (current_ret_.kind == ir::IRTypeKind::kVoid) {
          diag_.ReportError(rs->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby value return conflicts with the declared NilClass ABI");
          builder_.MakeUnreachable();
          terminated_ = true;
          return;
        }
        auto actual = InferExprType(rs->value);
        if (actual.kind == ir::IRTypeKind::kInvalid || !actual.SameShape(current_ret_)) {
          diag_.ReportError(rs->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby return value requires an unmodeled conversion to its YARD type");
          builder_.MakeUnreachable();
          terminated_ = true;
          return;
        }
        v = LowerExpr(rs->value, current_ret_);
      }
      if (v.empty()) {
        if (current_ret_.kind != ir::IRTypeKind::kVoid) {
          diag_.ReportError(rs->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby bare return requires a distinct nil runtime value");
          builder_.MakeUnreachable();
        } else {
          builder_.MakeReturn();
        }
      } else {
        builder_.MakeReturn(v);
      }
      terminated_ = true;
      return;
    }
    if (auto i = std::dynamic_pointer_cast<IfStmt>(s)) {
      if (!RequireBoolean(i->cond, i->unless ? "unless" : "if"))
        return;
      auto t_bb = builder_.CreateBlock("if.then");
      std::shared_ptr<ir::BasicBlock> e_bb =
          i->else_branch ? builder_.CreateBlock("if.else") : std::shared_ptr<ir::BasicBlock>{};
      auto c_bb = builder_.CreateBlock("if.end");
      auto cv = LowerExpr(i->cond, ir::IRType::I1());
      if (i->unless) {
        auto raw_one = builder_.MakeLiteral((long long)1)->name;
        auto one = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, raw_one, raw_one,
                                       "true")->name;
        cv = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, cv, one, "neg")->name;
      }
      builder_.MakeCondBranch(cv, t_bb.get(), e_bb ? e_bb.get() : c_bb.get());
      builder_.SetInsertPoint(t_bb);
      terminated_ = false;
      LowerStmt(i->then_branch, nullptr);
      if (!terminated_)
        builder_.MakeBranch(c_bb.get());
      if (e_bb) {
        builder_.SetInsertPoint(e_bb);
        terminated_ = false;
        LowerStmt(i->else_branch, nullptr);
        if (!terminated_)
          builder_.MakeBranch(c_bb.get());
      }
      builder_.SetInsertPoint(c_bb);
      terminated_ = false;
      return;
    }
    if (auto w = std::dynamic_pointer_cast<WhileStmt>(s)) {
      if (!RequireBoolean(w->cond, w->until ? "until" : "while"))
        return;
      auto cb = builder_.CreateBlock("w.cond");
      auto bb = builder_.CreateBlock("w.body");
      auto eb = builder_.CreateBlock("w.end");
      builder_.MakeBranch(cb.get());
      builder_.SetInsertPoint(cb);
      auto cv = LowerExpr(w->cond, ir::IRType::I1());
      if (w->until) {
        auto raw_one = builder_.MakeLiteral((long long)1)->name;
        auto one = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, raw_one, raw_one,
                                       "true")->name;
        cv = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, cv, one, "neg")->name;
      }
      builder_.MakeCondBranch(cv, bb.get(), eb.get());
      builder_.SetInsertPoint(bb);
      terminated_ = false;
      LowerStmt(w->body, nullptr);
      if (!terminated_)
        builder_.MakeBranch(cb.get());
      builder_.SetInsertPoint(eb);
      terminated_ = false;
      return;
    }
    if (auto c = std::dynamic_pointer_cast<CaseStmt>(s)) {
      bool has_pattern = false;
      for (const auto &branch : c->whens)
        has_pattern = has_pattern || branch.is_pattern;
      if (has_pattern) {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby pattern matching is parsed and analyzed but is not yet "
                          "lowerable to static Polyglot IR");
      } else {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby case statements are not yet lowerable to static Polyglot IR");
      }
      return;
    }
    diag_.ReportError(s->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "Ruby statement is parsed but has no faithful IR lowering");
  }

  static bool IsFloat(const ir::IRType &t) {
    return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
  }

  static bool IsInteger(const ir::IRType &t) { return t.IsInteger() && t.kind != ir::IRTypeKind::kI1; }

  ir::IRType InferExprType(const std::shared_ptr<Expression> &e) const {
    if (!e)
      return ir::IRType::Invalid();
    if (auto lit = std::dynamic_pointer_cast<Literal>(e)) {
      switch (lit->kind) {
      case Literal::Kind::kInt: return ir::IRType::I64();
      case Literal::Kind::kFloat: return ir::IRType::F64();
      case Literal::Kind::kBool: return ir::IRType::I1();
      case Literal::Kind::kString:
      case Literal::Kind::kSymbol: return ir::IRType::Pointer(ir::IRType::I8());
      default: return ir::IRType::Invalid();
      }
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
      auto it = locals_.find(id->name);
      return it == locals_.end() ? ir::IRType::Invalid() : it->second.type;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
      auto lhs = InferExprType(bin->left);
      auto rhs = InferExprType(bin->right);
      if (lhs.kind == ir::IRTypeKind::kInvalid || !lhs.SameShape(rhs))
        return ir::IRType::Invalid();
      if (bin->op == "&&" || bin->op == "and" || bin->op == "||" || bin->op == "or")
        return lhs.kind == ir::IRTypeKind::kI1 ? lhs : ir::IRType::Invalid();
      if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
          bin->op == ">" || bin->op == ">=")
        return lhs.IsScalar() ? ir::IRType::I1() : ir::IRType::Invalid();
      if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" ||
          bin->op == "%")
        return (IsFloat(lhs) || IsInteger(lhs)) ? lhs : ir::IRType::Invalid();
      if (bin->op == "&" || bin->op == "|" || bin->op == "^" || bin->op == "<<" ||
          bin->op == ">>")
        return IsInteger(lhs) ? lhs : ir::IRType::Invalid();
      return ir::IRType::Invalid();
    }
    if (auto unary = std::dynamic_pointer_cast<UnaryExpr>(e)) {
      auto operand = InferExprType(unary->operand);
      if (unary->op == "!" || unary->op == "not")
        return operand.kind == ir::IRTypeKind::kI1 ? ir::IRType::I1() : ir::IRType::Invalid();
      if (unary->op == "+" || unary->op == "-")
        return (IsFloat(operand) || IsInteger(operand)) ? operand : ir::IRType::Invalid();
      if (unary->op == "~")
        return IsInteger(operand) ? operand : ir::IRType::Invalid();
      return ir::IRType::Invalid();
    }
    if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
      if (!call->receiver && !call->block && !call->safe)
        if (const auto *api = frontends::FindNativeBuiltin(call->method))
          return frontends::NativeIRType(api->result);
      auto it = signatures_.find(call->method);
      if (!call->receiver && !call->block && !call->safe && it != signatures_.end())
        return ToIRType(it->second->return_type);
      return ir::IRType::Invalid();
    }
    if (auto assign = std::dynamic_pointer_cast<AssignExpr>(e)) {
      if (assign->op != "=" || !std::dynamic_pointer_cast<Identifier>(assign->target))
        return ir::IRType::Invalid();
      return InferExprType(assign->value);
    }
    return ir::IRType::Invalid();
  }

  bool RequireBoolean(const std::shared_ptr<Expression> &expression, const std::string &context) {
    if (InferExprType(expression).kind == ir::IRTypeKind::kI1)
      return true;
    diag_.ReportError(expression->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "Ruby " + context +
                          " condition requires object truthiness runtime semantics");
    return false;
  }

  std::string LowerExpr(const std::shared_ptr<Expression> &e, const ir::IRType &want) {
    if (!e)
      return "";
    if (auto lit = std::dynamic_pointer_cast<Literal>(e)) {
      switch (lit->kind) {
      case Literal::Kind::kInt: {
        std::string normalized;
        for (char ch : lit->value) {
          if (ch != '_')
            normalized.push_back(ch);
        }
        try {
          size_t consumed = 0;
          auto value = std::stoll(normalized, &consumed, 0);
          if (consumed != normalized.size())
            throw std::invalid_argument("trailing numeric characters");
          return builder_.MakeLiteral(value)->name;
        } catch (...) {
          diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby arbitrary-precision integer literal does not fit the IR ABI");
          return builder_.MakeLiteral((long long)0)->name;
        }
      }
      case Literal::Kind::kFloat: {
        std::string normalized;
        for (char ch : lit->value) {
          if (ch != '_')
            normalized.push_back(ch);
        }
        try {
          size_t consumed = 0;
          auto value = std::stod(normalized, &consumed);
          if (consumed != normalized.size())
            throw std::invalid_argument("trailing numeric characters");
          return builder_.MakeLiteral(value)->name;
        } catch (...) {
          diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby floating literal cannot be represented by the IR ABI");
          return builder_.MakeLiteral(0.0)->name;
        }
      }
      case Literal::Kind::kBool: {
        auto raw = builder_.MakeLiteral((long long)(lit->value == "true" ? 1 : 0))->name;
        auto one = builder_.MakeLiteral((long long)1)->name;
        return builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, raw, one, "bool")->name;
      }
      case Literal::Kind::kNil:
        diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby nil requires a distinct tagged runtime value");
        return builder_.MakeLiteral((long long)0)->name;
      case Literal::Kind::kString:
      case Literal::Kind::kSymbol:
        return builder_.MakeStringLiteral(lit->value, "rbstr");
      case Literal::Kind::kRegex:
        diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby Regexp literals require Regexp-object runtime lowering");
        return builder_.MakeLiteral((long long)0)->name;
      }
      return builder_.MakeLiteral((long long)0)->name;
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
      if (id->name == "...") {
        diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby anonymous argument forwarding requires call-frame lowering");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      auto it = locals_.find(id->name);
      if (it != locals_.end()) {
        auto load = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
        return load->name;
      }
      diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "unresolved Ruby identifier '" + id->name +
                            "' requires dynamic local/method/constant lookup");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
      if (bin->op == "&&" || bin->op == "and" || bin->op == "||" || bin->op == "or") {
        if (!RequireBoolean(bin->left, "logical operand") || !RequireBoolean(bin->right, "logical operand"))
          return "";
        const bool conjunction = bin->op == "&&" || bin->op == "and";
        auto left = LowerExpr(bin->left, ir::IRType::I1());
        auto left_end = builder_.GetInsertPoint();
        auto right = builder_.CreateBlock("logic.rhs"), merge = builder_.CreateBlock("logic.end");
        builder_.MakeCondBranch(left, conjunction ? right.get() : merge.get(), conjunction ? merge.get() : right.get());
        builder_.SetInsertPoint(right);
        auto value = LowerExpr(bin->right, ir::IRType::I1());
        auto right_end = builder_.GetInsertPoint();
        builder_.MakeBranch(merge.get());
        builder_.SetInsertPoint(merge);
        return builder_.MakePhi(ir::IRType::I1(), {{left_end.get(), left}, {right_end.get(), value}}, "logical")->name;
      }
      auto operand_type = InferExprType(bin->left);
      auto rhs_type = InferExprType(bin->right);
      if (operand_type.kind == ir::IRTypeKind::kInvalid || !operand_type.SameShape(rhs_type) ||
          (!IsFloat(operand_type) && !IsInteger(operand_type))) {
        diag_.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby binary operands require dynamic dispatch or numeric coercion");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      if (bin->op == "%" || (bin->op == "/" && IsInteger(operand_type))) {
        diag_.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby division/modulo floor semantics have no primitive IR equivalent");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      auto l = LowerExpr(bin->left, operand_type);
      auto r = LowerExpr(bin->right, operand_type);
      using Op = ir::BinaryInstruction::Op;
      bool fp = IsFloat(operand_type);
      Op op = Op::kAdd;
      bool cmp = false;
      const std::string &o = bin->op;
      if (o == "+")
        op = fp ? Op::kFAdd : Op::kAdd;
      else if (o == "-")
        op = fp ? Op::kFSub : Op::kSub;
      else if (o == "*")
        op = fp ? Op::kFMul : Op::kMul;
      else if (o == "/")
        op = fp ? Op::kFDiv : Op::kSDiv;
      else if (o == "%")
        op = fp ? Op::kFRem : Op::kSRem;
      else if (o == "&")
        op = Op::kAnd;
      else if (o == "|")
        op = Op::kOr;
      else if (o == "^")
        op = Op::kXor;
      else if (o == "<<")
        op = Op::kShl;
      else if (o == ">>")
        op = Op::kAShr;
      else if (o == "==") {
        op = fp ? Op::kCmpFoe : Op::kCmpEq;
        cmp = true;
      } else if (o == "!=") {
        op = Op::kCmpNe;
        cmp = true;
      } else if (o == "<") {
        op = fp ? Op::kCmpFlt : Op::kCmpSlt;
        cmp = true;
      } else if (o == "<=") {
        op = fp ? Op::kCmpFle : Op::kCmpSle;
        cmp = true;
      } else if (o == ">") {
        op = fp ? Op::kCmpFgt : Op::kCmpSgt;
        cmp = true;
      } else if (o == ">=") {
        op = fp ? Op::kCmpFge : Op::kCmpSge;
        cmp = true;
      } else {
        diag_.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby binary operator '" + o +
                              "' has no faithful static IR lowering");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      return builder_.MakeBinary(op, l, r, cmp ? "cmp" : "bop")->name;
    }
    if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
      auto operand_type = InferExprType(u->operand);
      if (operand_type.kind == ir::IRTypeKind::kInvalid) {
        diag_.ReportError(u->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby unary operand requires dynamic conversion semantics");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      auto v = LowerExpr(u->operand, operand_type);
      if (u->op == "-") {
        if (!IsFloat(operand_type) && !IsInteger(operand_type)) {
          diag_.ReportError(u->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby unary minus requires numeric method dispatch");
          return builder_.MakeLiteral((long long)0)->name;
        }
        if (IsFloat(operand_type)) return builder_.MakeFloatNegate(v, operand_type, "neg")->name;
        auto z = builder_.MakeLiteral((long long)0)->name;
        return builder_
            .MakeBinary(IsFloat(operand_type) ? ir::BinaryInstruction::Op::kFSub
                                             : ir::BinaryInstruction::Op::kSub,
                        z, v, "neg")
            ->name;
      }
      if (u->op == "!" || u->op == "not") {
        if (operand_type.kind != ir::IRTypeKind::kI1) {
          diag_.ReportError(u->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby logical not requires object truthiness runtime semantics");
          return builder_.MakeLiteral((long long)0)->name;
        }
        auto raw_one = builder_.MakeLiteral((long long)1)->name;
        auto one = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, raw_one, raw_one,
                                       "true")->name;
        return builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, v, one, "not")->name;
      }
      if (u->op == "+" && (IsFloat(operand_type) || IsInteger(operand_type)))
        return v;
      if (u->op == "~") {
        if (!IsInteger(operand_type)) {
          diag_.ReportError(u->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby bitwise not requires Integer method dispatch");
          return builder_.MakeLiteral((long long)0)->name;
        }
        auto all_ones = builder_.MakeLiteral((long long)-1)->name;
        return builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, v, all_ones, "bnot")->name;
      }
      diag_.ReportError(u->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Ruby unary operator '" + u->op + "' requires runtime semantics");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    if (auto a = std::dynamic_pointer_cast<AssignExpr>(e)) {
      if (a->op != "=" && a->op != "=>") {
        diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby compound assignment requires read-modify-write runtime lowering");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      auto rhs_type = InferExprType(a->value);
      if (rhs_type.kind == ir::IRTypeKind::kInvalid) {
        diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby assignment value has no modeled static type");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      auto rhs = LowerExpr(a->value, rhs_type);
      if (auto id = std::dynamic_pointer_cast<Identifier>(a->target)) {
        auto it = locals_.find(id->name);
        if (it == locals_.end()) {
          // Implicit declaration in Ruby
          auto al = builder_.MakeAlloca(rhs_type, id->name + ".addr");
          locals_[id->name] = {id->name + ".addr", rhs_type};
          it = locals_.find(id->name);
        } else if (!it->second.type.SameShape(rhs_type)) {
          diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby local changes runtime type and needs tagged-value lowering");
          return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                               : builder_.MakeLiteral((long long)0)->name;
        }
        builder_.MakeStore(it->second.addr, rhs);
      } else {
        diag_.ReportError(a->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby non-local assignment target requires dynamic setter semantics");
      }
      return rhs;
    }
    if (auto member = std::dynamic_pointer_cast<MemberExpr>(e)) {
      diag_.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                        member->safe ? "Ruby safe navigation requires dynamic runtime lowering"
                                     : "Ruby member access requires dynamic dispatch lowering");
      return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                           : builder_.MakeLiteral((long long)0)->name;
    }
    if (auto c = std::dynamic_pointer_cast<CallExpr>(e)) {
      if (c->block) {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby calls with blocks require closure/yield runtime lowering");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      if (c->safe) {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby safe-navigation calls require dynamic runtime lowering");
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
      }
      if (!c->receiver) if (const auto *api = frontends::FindNativeBuiltin(c->method)) {
        std::vector<std::string> values;
        std::vector<ir::IRType> types;
        for (const auto &arg : c->args) {
          if (values.size() < api->params.size() && api->params[values.size()] == frontends::NativeType::kString) {
            if (auto literal = std::dynamic_pointer_cast<Literal>(arg)) {
              std::string text, error;
              const auto &source = literal->value;
              if (literal->kind != Literal::Kind::kString || literal->is_heredoc || source.size() < 2 ||
                  (source.front() != '\'' && source.front() != '"') || source.back() != source.front()) {
                error = "Ruby native text APIs require an ordinary quoted string literal";
              } else if (source.front() == '\'') {
                for (size_t i = 1; i + 1 < source.size(); ++i) {
                  if (source[i] == '\\' && i + 2 < source.size() && (source[i+1] == '\\' || source[i+1] == '\'')) ++i;
                  text.push_back(source[i]);
                }
              } else {
                const auto body = source.substr(1, source.size() - 2);
                if (body.find("#{") != std::string::npos) error = "Ruby native string interpolation is not supported";
                for (size_t i = 0; i < body.size() && error.empty(); ++i) {
                  if (body[i] != '\\') continue;
                  if (++i == body.size()) { error = "unfinished Ruby string escape"; break; }
                  const char escape = body[i];
                  if (std::string("nrtabfv\\\"'u").find(escape) == std::string::npos)
                    error = "unsupported Ruby native string escape";
                  if (escape == 'u') i += 4;
                }
                if (error.empty()) frontends::DecodeNativeString(body, false, false, text, error);
              }
              if (text.find('\0') != std::string::npos) error = "native string APIs do not accept embedded NUL bytes";
              if (!error.empty()) {
                diag_.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering, error);
                return "";
              }
              values.push_back(builder_.MakeStringLiteral(text, "native.rbstr"));
              types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
              continue;
            }
          }
          auto type = InferExprType(arg);
          types.push_back(type); values.push_back(LowerExpr(arg, type));
        }
        auto inst = frontends::EmitNativeBuiltin(*api, values, types, builder_, ctx_, diag_, c->loc);
        return inst ? inst->name : "";
      }
      auto it = signatures_.find(c->method);
      if (c->receiver || it == signatures_.end()) {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby call requires a local typed method with no dynamic receiver");
        return "";
      }
      const auto &method = *it->second;
      if (c->args.size() != method.params.size()) {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Ruby static call argument count mismatch");
        return "";
      }
      std::vector<std::string> args;
      for (std::size_t i = 0; i < c->args.size(); ++i) {
        const auto type = ToIRType(method.params[i].type);
        if (!InferExprType(c->args[i]).SameShape(type)) {
          diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Ruby static call argument type mismatch");
          return "";
        }
        args.push_back(LowerExpr(c->args[i], type));
      }
      return builder_.MakeCall(c->method, args, ToIRType(method.return_type))->name;
    }
    diag_.ReportError(e->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "Ruby expression is parsed but has no faithful IR lowering");
    return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                         : builder_.MakeLiteral((long long)0)->name;
  }

  struct Local {
    std::string addr;
    ir::IRType type;
  };
  const Module &module_;
  ir::IRContext &ctx_;
  ir::IRBuilder builder_;
  [[maybe_unused]] frontends::Diagnostics &diag_;
  std::unordered_map<std::string, Local> locals_;
  std::unordered_map<std::string, const MethodDecl *> signatures_;
  ir::IRType current_ret_{ir::IRType::Void()};
  ir::IRType last_expr_type_{ir::IRType::Invalid()};
  bool terminated_{false};
};

} // namespace

void LowerToIR(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag) {
  Lowerer(mod, ctx, diag).Run();
}

} // namespace polyglot::ruby
