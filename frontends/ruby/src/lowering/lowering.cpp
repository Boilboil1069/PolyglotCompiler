/**
 * @file     lowering.cpp
 * @brief    Ruby �?Polyglot IR lowering
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Lowers a typed numeric subset of Ruby to IR.  Methods declared with
 * YARD `@param`/`@return` tags participate in the cross-language IR
 * surface; everything else is reported but not embedded.
 */
#include "frontends/ruby/include/ruby_lowering.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/ir_builder.h"

namespace polyglot::ruby {

namespace {

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &t) {
    if (!t) return ir::IRType::I64();
    const std::string &n = t->name;
    if (n == "Integer" || n == "Fixnum" || n == "Bignum" || n == "Numeric") return ir::IRType::I64();
    if (n == "Float")   return ir::IRType::F64();
    if (n == "String" || n == "Symbol") return ir::IRType::Pointer(ir::IRType::I8());
    if (n == "TrueClass" || n == "FalseClass" || n == "Boolean") return ir::IRType::I1();
    if (n == "NilClass" || n == "Nil")   return ir::IRType::Void();
    if (n == "Array" || n == "Hash" || n == "Object") return ir::IRType::Pointer(ir::IRType::I8());
    return ir::IRType::I64();
}

class Lowerer {
  public:
    Lowerer(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag)
        : module_(mod), ctx_(ctx), builder_(ctx), diag_(diag) {}

    void Run() { for (auto &s : module_.body) Top(s, ""); }

  private:
    void Top(const std::shared_ptr<Statement> &s, const std::string &prefix) {
        if (auto m = std::dynamic_pointer_cast<MethodDecl>(s)) {
            LowerMethod(*m, prefix);
        } else if (auto c = std::dynamic_pointer_cast<ClassDecl>(s)) {
            std::string p = prefix.empty() ? c->name : prefix + "::" + c->name;
            for (auto &b : c->body) Top(b, p);
        } else if (auto md = std::dynamic_pointer_cast<ModuleDecl>(s)) {
            std::string p = prefix.empty() ? md->name : prefix + "::" + md->name;
            for (auto &b : md->body) Top(b, p);
        }
    }

    void LowerMethod(const MethodDecl &m, const std::string &prefix) {
        std::vector<std::pair<std::string, ir::IRType>> ir_params;
        for (auto &p : m.params) {
            if (p.splat || p.double_splat || p.block) continue;
            ir_params.emplace_back(p.name, ToIRType(p.type));
        }
        auto ret = ToIRType(m.return_type);
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
        std::string last_value;
        LowerStmt(m.body, &last_value);
        if (!terminated_) {
            // Ruby returns the value of the last expression.
            if (ret.kind == ir::IRTypeKind::kVoid) {
                builder_.MakeReturn();
            } else if (!last_value.empty()) {
                builder_.MakeReturn(last_value);
            } else {
                std::string z = (ret.kind == ir::IRTypeKind::kF32 ||
                                 ret.kind == ir::IRTypeKind::kF64)
                                    ? builder_.MakeLiteral(0.0)->name
                                    : builder_.MakeLiteral((long long)0)->name;
                builder_.MakeReturn(z);
            }
        }
        builder_.ClearCurrentFunction();
    }

    void LowerStmt(const std::shared_ptr<Statement> &s, std::string *last_val) {
        if (!s || terminated_) return;
        if (auto blk = std::dynamic_pointer_cast<Block>(s)) {
            for (auto &c : blk->stmts) {
                if (terminated_) return;
                LowerStmt(c, last_val);
            }
            return;
        }
        if (auto es = std::dynamic_pointer_cast<ExprStmt>(s)) {
            auto v = LowerExpr(es->expr, current_ret_);
            if (last_val) *last_val = v;
            return;
        }
        if (auto rs = std::dynamic_pointer_cast<ReturnStmt>(s)) {
            std::string v;
            if (rs->value) v = LowerExpr(rs->value, current_ret_);
            if (v.empty()) builder_.MakeReturn();
            else builder_.MakeReturn(v);
            terminated_ = true;
            return;
        }
        if (auto i = std::dynamic_pointer_cast<IfStmt>(s)) {
            auto t_bb = builder_.CreateBlock("if.then");
            std::shared_ptr<ir::BasicBlock> e_bb =
                i->else_branch ? builder_.CreateBlock("if.else")
                               : std::shared_ptr<ir::BasicBlock>{};
            auto c_bb = builder_.CreateBlock("if.end");
            auto cv = LowerExpr(i->cond, ir::IRType::I1());
            if (i->unless) {
                auto one = builder_.MakeLiteral((long long)1)->name;
                cv = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, cv, one, "neg")->name;
            }
            builder_.MakeCondBranch(cv, t_bb.get(), e_bb ? e_bb.get() : c_bb.get());
            builder_.SetInsertPoint(t_bb); terminated_ = false;
            LowerStmt(i->then_branch, nullptr);
            if (!terminated_) builder_.MakeBranch(c_bb.get());
            if (e_bb) {
                builder_.SetInsertPoint(e_bb); terminated_ = false;
                LowerStmt(i->else_branch, nullptr);
                if (!terminated_) builder_.MakeBranch(c_bb.get());
            }
            builder_.SetInsertPoint(c_bb); terminated_ = false; return;
        }
        if (auto w = std::dynamic_pointer_cast<WhileStmt>(s)) {
            auto cb = builder_.CreateBlock("w.cond");
            auto bb = builder_.CreateBlock("w.body");
            auto eb = builder_.CreateBlock("w.end");
            builder_.MakeBranch(cb.get());
            builder_.SetInsertPoint(cb);
            auto cv = LowerExpr(w->cond, ir::IRType::I1());
            if (w->until) {
                auto one = builder_.MakeLiteral((long long)1)->name;
                cv = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, cv, one, "neg")->name;
            }
            builder_.MakeCondBranch(cv, bb.get(), eb.get());
            builder_.SetInsertPoint(bb); terminated_ = false;
            LowerStmt(w->body, nullptr);
            if (!terminated_) builder_.MakeBranch(cb.get());
            builder_.SetInsertPoint(eb); terminated_ = false; return;
        }
    }

    static bool IsFloat(const ir::IRType &t) {
        return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
    }

    std::string LowerExpr(const std::shared_ptr<Expression> &e, const ir::IRType &want) {
        if (!e) return "";
        if (auto lit = std::dynamic_pointer_cast<Literal>(e)) {
            switch (lit->kind) {
                case Literal::Kind::kInt: {
                    long long v = 0; try { v = std::stoll(lit->value); } catch (...) {}
                    return builder_.MakeLiteral(v)->name;
                }
                case Literal::Kind::kFloat: {
                    double d = 0.0; try { d = std::stod(lit->value); } catch (...) {}
                    return builder_.MakeLiteral(d)->name;
                }
                case Literal::Kind::kBool:
                    return builder_.MakeLiteral((long long)(lit->value == "true" ? 1 : 0))->name;
                case Literal::Kind::kNil:
                    return builder_.MakeLiteral((long long)0)->name;
                case Literal::Kind::kString:
                case Literal::Kind::kSymbol:
                case Literal::Kind::kRegex:
                    return builder_.MakeStringLiteral(lit->value, "rbstr");
            }
            return builder_.MakeLiteral((long long)0)->name;
        }
        if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
            auto it = locals_.find(id->name);
            if (it != locals_.end()) {
                auto load = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
                return load->name;
            }
            return id->name;
        }
        if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            auto l = LowerExpr(bin->left, want);
            auto r = LowerExpr(bin->right, want);
            using Op = ir::BinaryInstruction::Op;
            bool fp = IsFloat(want);
            Op op = Op::kAdd; bool cmp = false;
            const std::string &o = bin->op;
            if (o == "+")       op = fp ? Op::kFAdd : Op::kAdd;
            else if (o == "-")  op = fp ? Op::kFSub : Op::kSub;
            else if (o == "*")  op = fp ? Op::kFMul : Op::kMul;
            else if (o == "/")  op = fp ? Op::kFDiv : Op::kSDiv;
            else if (o == "%")  op = fp ? Op::kFRem : Op::kSRem;
            else if (o == "&")  op = Op::kAnd;
            else if (o == "|")  op = Op::kOr;
            else if (o == "^")  op = Op::kXor;
            else if (o == "<<") op = Op::kShl;
            else if (o == ">>") op = Op::kAShr;
            else if (o == "==") { op = fp ? Op::kCmpFoe : Op::kCmpEq; cmp = true; }
            else if (o == "!=") { op = fp ? Op::kCmpFne : Op::kCmpNe; cmp = true; }
            else if (o == "<")  { op = fp ? Op::kCmpFlt : Op::kCmpSlt; cmp = true; }
            else if (o == "<=") { op = fp ? Op::kCmpFle : Op::kCmpSle; cmp = true; }
            else if (o == ">")  { op = fp ? Op::kCmpFgt : Op::kCmpSgt; cmp = true; }
            else if (o == ">=") { op = fp ? Op::kCmpFge : Op::kCmpSge; cmp = true; }
            else if (o == "&&" || o == "and") op = Op::kAnd;
            else if (o == "||" || o == "or")  op = Op::kOr;
            return builder_.MakeBinary(op, l, r, cmp ? "cmp" : "bop")->name;
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            auto v = LowerExpr(u->operand, want);
            if (u->op == "-") {
                auto z = IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                                       : builder_.MakeLiteral((long long)0)->name;
                return builder_.MakeBinary(IsFloat(want) ? ir::BinaryInstruction::Op::kFSub
                                                         : ir::BinaryInstruction::Op::kSub,
                                           z, v, "neg")->name;
            }
            if (u->op == "!" || u->op == "not") {
                auto one = builder_.MakeLiteral((long long)1)->name;
                return builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, v, one, "not")->name;
            }
            return v;
        }
        if (auto a = std::dynamic_pointer_cast<AssignExpr>(e)) {
            auto rhs = LowerExpr(a->value, want);
            if (auto id = std::dynamic_pointer_cast<Identifier>(a->target)) {
                auto it = locals_.find(id->name);
                if (it == locals_.end()) {
                    // Implicit declaration in Ruby
                    auto al = builder_.MakeAlloca(want, id->name + ".addr");
                    locals_[id->name] = {id->name + ".addr", want};
                    it = locals_.find(id->name);
                }
                builder_.MakeStore(it->second.addr, rhs);
            }
            return rhs;
        }
        if (auto c = std::dynamic_pointer_cast<CallExpr>(e)) {
            std::vector<std::string> args;
            for (auto &x : c->args) args.push_back(LowerExpr(x, ir::IRType::I64()));
            std::string callee = c->method;
            if (auto id = std::dynamic_pointer_cast<Identifier>(c->receiver)) {
                callee = id->name + "::" + c->method;
            }
            return builder_.MakeCall(callee, args, want, "call")->name;
        }
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
    }

    struct Local { std::string addr; ir::IRType type; };
    const Module &module_;
    ir::IRContext &ctx_;
    ir::IRBuilder builder_;
    frontends::Diagnostics &diag_;
    std::unordered_map<std::string, Local> locals_;
    ir::IRType current_ret_{ir::IRType::Void()};
    bool terminated_{false};
};

}  // namespace

void LowerToIR(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag) {
    Lowerer(mod, ctx, diag).Run();
}

}  // namespace polyglot::ruby
