/**
 * @file     lowering.cpp
 * @brief    Go → Polyglot IR lowering
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Lowers the statically typed numeric/boolean subset of Go to IR:
 *   - Top-level `func` declarations with declared parameter and return
 *     types.  Multi-return functions surface their first declared
 *     result type as the IR return type.
 *   - Local `var` / `:=` declarations of basic types.
 *   - if / for (three-clause and condition-only) / return / assignment
 *     and the standard arithmetic/comparison/logical operators.
 *
 * Anything outside that subset (channels, goroutines, interfaces …) is
 * skipped silently — the polyglot compiler treats Go primarily as a
 * source of foreign function signatures.
 */
#include "frontends/go/include/go_lowering.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/ir_builder.h"

namespace polyglot::go {

namespace {

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &t) {
    if (!t) return ir::IRType::Void();
    if (t->kind == TypeKind::kPointer) return ir::IRType::Pointer(ToIRType(t->elem));
    if (t->kind == TypeKind::kSlice || t->kind == TypeKind::kArray ||
        t->kind == TypeKind::kMap   || t->kind == TypeKind::kChan ||
        t->kind == TypeKind::kFunc  || t->kind == TypeKind::kInterface ||
        t->kind == TypeKind::kStruct)
        return ir::IRType::Pointer(ir::IRType::I8());
    if (t->kind != TypeKind::kNamed) return ir::IRType::Pointer(ir::IRType::I8());
    const std::string &n = t->name;
    if (n == "bool") return ir::IRType::I1();
    if (n == "int8" || n == "byte" || n == "uint8") return ir::IRType::I8();
    if (n == "int16" || n == "uint16") return ir::IRType::I16();
    if (n == "int32" || n == "rune" || n == "uint32") return ir::IRType::I32();
    if (n == "int" || n == "int64" || n == "uint" || n == "uint64" ||
        n == "uintptr") return ir::IRType::I64();
    if (n == "float32") return ir::IRType::F32();
    if (n == "float64") return ir::IRType::F64();
    if (n == "string")  return ir::IRType::Pointer(ir::IRType::I8());
    if (n == "error")   return ir::IRType::Pointer(ir::IRType::I8());
    return ir::IRType::Pointer(ir::IRType::I8());
}

class Lowerer {
  public:
    Lowerer(const File &f, ir::IRContext &c, frontends::Diagnostics &d)
        : file_(f), ctx_(c), builder_(c), diag_(d) {}

    void Run() { for (auto &fn : file_.funcs) LowerFunc(*fn); }

  private:
    void LowerFunc(const FuncDecl &f) {
        if (!f.body) return;
        std::vector<std::pair<std::string, ir::IRType>> ir_params;
        if (f.receiver) {
            ir_params.emplace_back(f.receiver->name.empty() ? "self" : f.receiver->name,
                                   ToIRType(f.receiver->type));
        }
        int unnamed_idx = 0;
        for (auto &p : f.params) {
            std::string n = p.first.empty() ? "_arg" + std::to_string(unnamed_idx++) : p.first;
            ir_params.emplace_back(n, ToIRType(p.second));
        }
        ir::IRType ret = ir::IRType::Void();
        if (f.results.size() == 1) ret = ToIRType(f.results.front().second);
        else if (f.results.size() > 1) ret = ToIRType(f.results.front().second);

        std::string name = f.name;
        if (f.receiver && f.receiver->type) {
            std::string recv = f.receiver->type->name;
            if (f.receiver->type->kind == TypeKind::kPointer && f.receiver->type->elem)
                recv = f.receiver->type->elem->name;
            if (!recv.empty()) name = recv + "." + f.name;
        }
        if (!file_.package_name.empty() && file_.package_name != "main")
            name = file_.package_name + "." + name;

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
        if (f.body) for (auto &s : f.body->stmts) LowerStmt(s);
        if (!terminated_) {
            if (ret.kind == ir::IRTypeKind::kVoid) builder_.MakeReturn();
            else if (ret.kind == ir::IRTypeKind::kF32 || ret.kind == ir::IRTypeKind::kF64)
                builder_.MakeReturn(builder_.MakeLiteral(0.0)->name);
            else
                builder_.MakeReturn(builder_.MakeLiteral((long long)0)->name);
        }
        builder_.ClearCurrentFunction();
    }

    void LowerStmt(const std::shared_ptr<Statement> &s) {
        if (!s || terminated_) return;
        if (auto b = std::dynamic_pointer_cast<Block>(s)) {
            for (auto &c : b->stmts) { if (terminated_) return; LowerStmt(c); }
            return;
        }
        if (auto e = std::dynamic_pointer_cast<ExprStmt>(s)) {
            LowerExpr(e->expr, ir::IRType::I64()); return;
        }
        if (auto a = std::dynamic_pointer_cast<AssignStmt>(s)) {
            LowerAssign(*a); return;
        }
        if (auto r = std::dynamic_pointer_cast<ReturnStmt>(s)) {
            if (r->results.empty()) {
                if (current_ret_.kind == ir::IRTypeKind::kVoid) builder_.MakeReturn();
                else builder_.MakeReturn(builder_.MakeLiteral((long long)0)->name);
            } else {
                auto v = LowerExpr(r->results.front(), current_ret_);
                builder_.MakeReturn(v);
            }
            terminated_ = true;
            return;
        }
        if (auto i = std::dynamic_pointer_cast<IfStmt>(s)) { LowerIf(*i); return; }
        if (auto f = std::dynamic_pointer_cast<ForStmt>(s)) { LowerFor(*f); return; }
        if (auto inc = std::dynamic_pointer_cast<IncDecStmt>(s)) {
            // Treat as "x = x ± 1"
            if (auto id = std::dynamic_pointer_cast<Identifier>(inc->target)) {
                auto it = locals_.find(id->name);
                if (it == locals_.end()) return;
                auto cur = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
                auto one = IsFloat(it->second.type)
                               ? builder_.MakeLiteral(1.0)->name
                               : builder_.MakeLiteral((long long)1)->name;
                using Op = ir::BinaryInstruction::Op;
                Op op = inc->inc
                            ? (IsFloat(it->second.type) ? Op::kFAdd : Op::kAdd)
                            : (IsFloat(it->second.type) ? Op::kFSub : Op::kSub);
                auto nv = builder_.MakeBinary(op, cur->name, one, "incdec");
                builder_.MakeStore(it->second.addr, nv->name);
            }
            return;
        }
        if (auto ds = std::dynamic_pointer_cast<DeclStmt>(s)) {
            if (ds->decl && ds->decl->keyword == "var") {
                for (auto &v : ds->decl->values) {
                    auto ty = ToIRType(v.type);
                    for (size_t k = 0; k < v.names.size(); ++k) {
                        auto al = builder_.MakeAlloca(ty, v.names[k] + ".addr");
                        locals_[v.names[k]] = {v.names[k] + ".addr", ty};
                        if (k < v.values.size()) {
                            auto rv = LowerExpr(v.values[k], ty);
                            builder_.MakeStore(v.names[k] + ".addr", rv);
                        }
                    }
                }
            }
            return;
        }
    }

    void LowerAssign(const AssignStmt &a) {
        if (a.lhs.empty() || a.rhs.empty()) return;
        // Only handle 1:1 simple assignments
        for (size_t k = 0; k < a.lhs.size() && k < a.rhs.size(); ++k) {
            auto id = std::dynamic_pointer_cast<Identifier>(a.lhs[k]);
            if (!id) continue;
            ir::IRType ty = ir::IRType::I64();
            auto it = locals_.find(id->name);
            if (it != locals_.end()) ty = it->second.type;
            auto rv = LowerExpr(a.rhs[k], ty);
            if (a.op == ":=" || it == locals_.end()) {
                builder_.MakeAlloca(ty, id->name + ".addr");
                locals_[id->name] = {id->name + ".addr", ty};
            }
            if (a.op != "=" && a.op != ":=" && it != locals_.end()) {
                auto cur = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
                using Op = ir::BinaryInstruction::Op;
                Op op = Op::kAdd;
                bool fp = IsFloat(it->second.type);
                if (a.op == "+=") op = fp ? Op::kFAdd : Op::kAdd;
                else if (a.op == "-=") op = fp ? Op::kFSub : Op::kSub;
                else if (a.op == "*=") op = fp ? Op::kFMul : Op::kMul;
                else if (a.op == "/=") op = fp ? Op::kFDiv : Op::kSDiv;
                else if (a.op == "%=") op = fp ? Op::kFRem : Op::kSRem;
                else if (a.op == "&=") op = Op::kAnd;
                else if (a.op == "|=") op = Op::kOr;
                else if (a.op == "^=") op = Op::kXor;
                else if (a.op == "<<=") op = Op::kShl;
                else if (a.op == ">>=") op = Op::kAShr;
                rv = builder_.MakeBinary(op, cur->name, rv, "augop")->name;
            }
            builder_.MakeStore(locals_[id->name].addr, rv);
        }
    }

    void LowerIf(const IfStmt &i) {
        if (i.init) LowerStmt(i.init);
        auto t_bb = builder_.CreateBlock("if.then");
        std::shared_ptr<ir::BasicBlock> e_bb =
            i.else_branch ? builder_.CreateBlock("if.else")
                          : std::shared_ptr<ir::BasicBlock>{};
        auto c_bb = builder_.CreateBlock("if.end");
        auto cv = LowerExpr(i.cond, ir::IRType::I1());
        builder_.MakeCondBranch(cv, t_bb.get(), e_bb ? e_bb.get() : c_bb.get());
        builder_.SetInsertPoint(t_bb); terminated_ = false;
        if (i.body) for (auto &s : i.body->stmts) LowerStmt(s);
        if (!terminated_) builder_.MakeBranch(c_bb.get());
        if (e_bb) {
            builder_.SetInsertPoint(e_bb); terminated_ = false;
            LowerStmt(i.else_branch);
            if (!terminated_) builder_.MakeBranch(c_bb.get());
        }
        builder_.SetInsertPoint(c_bb); terminated_ = false;
    }

    void LowerFor(const ForStmt &f) {
        if (f.init) LowerStmt(f.init);
        auto cb = builder_.CreateBlock("for.cond");
        auto bb = builder_.CreateBlock("for.body");
        auto pb = f.post ? builder_.CreateBlock("for.post")
                         : std::shared_ptr<ir::BasicBlock>{};
        auto eb = builder_.CreateBlock("for.end");
        builder_.MakeBranch(cb.get());
        builder_.SetInsertPoint(cb);
        if (f.cond) {
            auto cv = LowerExpr(f.cond, ir::IRType::I1());
            builder_.MakeCondBranch(cv, bb.get(), eb.get());
        } else {
            builder_.MakeBranch(bb.get());
        }
        builder_.SetInsertPoint(bb); terminated_ = false;
        if (f.body) for (auto &s : f.body->stmts) LowerStmt(s);
        if (!terminated_) builder_.MakeBranch(pb ? pb.get() : cb.get());
        if (pb) {
            builder_.SetInsertPoint(pb); terminated_ = false;
            LowerStmt(f.post);
            if (!terminated_) builder_.MakeBranch(cb.get());
        }
        builder_.SetInsertPoint(eb); terminated_ = false;
    }

    static bool IsFloat(const ir::IRType &t) {
        return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
    }

    std::string LowerExpr(const std::shared_ptr<Expression> &e, const ir::IRType &want) {
        if (!e) return "";
        if (auto lit = std::dynamic_pointer_cast<BasicLit>(e)) {
            switch (lit->kind) {
                case BasicLit::Kind::kInt: {
                    long long v = 0; try { v = std::stoll(lit->value, nullptr, 0); } catch (...) {}
                    return builder_.MakeLiteral(v)->name;
                }
                case BasicLit::Kind::kFloat:
                case BasicLit::Kind::kImag: {
                    double d = 0.0; try { d = std::stod(lit->value); } catch (...) {}
                    return builder_.MakeLiteral(d)->name;
                }
                case BasicLit::Kind::kBool:
                    return builder_.MakeLiteral((long long)(lit->value == "true" ? 1 : 0))->name;
                case BasicLit::Kind::kNil:
                    return builder_.MakeLiteral((long long)0)->name;
                case BasicLit::Kind::kString:
                case BasicLit::Kind::kRune:
                    return builder_.MakeStringLiteral(lit->value, "gostr");
            }
        }
        if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
            auto it = locals_.find(id->name);
            if (it != locals_.end()) {
                auto load = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
                return load->name;
            }
            return id->name;
        }
        if (auto p = std::dynamic_pointer_cast<ParenExpr>(e))
            return LowerExpr(p->inner, want);
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            auto v = LowerExpr(u->operand, want);
            using Op = ir::BinaryInstruction::Op;
            if (u->op == "-") {
                auto z = IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                                       : builder_.MakeLiteral((long long)0)->name;
                return builder_.MakeBinary(IsFloat(want) ? Op::kFSub : Op::kSub, z, v, "neg")->name;
            }
            if (u->op == "!") {
                auto one = builder_.MakeLiteral((long long)1)->name;
                return builder_.MakeBinary(Op::kXor, v, one, "not")->name;
            }
            if (u->op == "^") {
                auto neg = builder_.MakeLiteral((long long)-1)->name;
                return builder_.MakeBinary(Op::kXor, v, neg, "bnot")->name;
            }
            return v;
        }
        if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            using Op = ir::BinaryInstruction::Op;
            bool fp = IsFloat(want);
            const std::string &o = bin->op;
            // Short-circuit: lower as eager bitwise for simplicity in IR slice
            auto l = LowerExpr(bin->left, want);
            auto r = LowerExpr(bin->right, want);
            Op op = Op::kAdd; bool cmp = false;
            if (o == "+") op = fp ? Op::kFAdd : Op::kAdd;
            else if (o == "-") op = fp ? Op::kFSub : Op::kSub;
            else if (o == "*") op = fp ? Op::kFMul : Op::kMul;
            else if (o == "/") op = fp ? Op::kFDiv : Op::kSDiv;
            else if (o == "%") op = fp ? Op::kFRem : Op::kSRem;
            else if (o == "&" || o == "&&") op = Op::kAnd;
            else if (o == "|" || o == "||") op = Op::kOr;
            else if (o == "^") op = Op::kXor;
            else if (o == "<<") op = Op::kShl;
            else if (o == ">>") op = Op::kAShr;
            else if (o == "==") { op = fp ? Op::kCmpFoe : Op::kCmpEq; cmp = true; }
            else if (o == "!=") { op = fp ? Op::kCmpFne : Op::kCmpNe; cmp = true; }
            else if (o == "<")  { op = fp ? Op::kCmpFlt : Op::kCmpSlt; cmp = true; }
            else if (o == "<=") { op = fp ? Op::kCmpFle : Op::kCmpSle; cmp = true; }
            else if (o == ">")  { op = fp ? Op::kCmpFgt : Op::kCmpSgt; cmp = true; }
            else if (o == ">=") { op = fp ? Op::kCmpFge : Op::kCmpSge; cmp = true; }
            return builder_.MakeBinary(op, l, r, cmp ? "cmp" : "bop")->name;
        }
        if (auto c = std::dynamic_pointer_cast<CallExpr>(e)) {
            std::vector<std::string> args;
            for (auto &a : c->args) args.push_back(LowerExpr(a, ir::IRType::I64()));
            std::string callee;
            if (auto id = std::dynamic_pointer_cast<Identifier>(c->fun)) callee = id->name;
            else if (auto sel = std::dynamic_pointer_cast<SelectorExpr>(c->fun)) {
                if (auto ix = std::dynamic_pointer_cast<Identifier>(sel->x))
                    callee = ix->name + "." + sel->sel;
                else callee = sel->sel;
            } else callee = "indirect_call";
            return builder_.MakeCall(callee, args, want, "call")->name;
        }
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
    }

    struct Local { std::string addr; ir::IRType type; };
    const File &file_;
    ir::IRContext &ctx_;
    ir::IRBuilder builder_;
    frontends::Diagnostics &diag_;
    std::unordered_map<std::string, Local> locals_;
    ir::IRType current_ret_{ir::IRType::Void()};
    bool terminated_{false};
};

}  // namespace

void LowerToIR(const File &f, ir::IRContext &c, frontends::Diagnostics &d) {
    Lowerer(f, c, d).Run();
}

}  // namespace polyglot::go
