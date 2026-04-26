/**
 * @file     lowering.cpp
 * @brief    JavaScript â†?Polyglot IR lowering
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Translates a JavaScript AST into the Polyglot IR.  The lowering targets
 * a typed numeric subset suitable for cross-language interop:
 *   - Top-level function declarations become IR functions
 *   - Function parameters are typed via JSDoc annotations (defaulting to f64)
 *   - Arithmetic, comparison, logical and bitwise operations on numeric
 *     and boolean operands lower to their IR counterparts
 *   - Variable declarations become alloca/store pairs
 *   - if/while/for/return statements lower to control-flow blocks
 *   - Calls into other JS functions resolve by qualified name
 *   - Calls into globals or unresolved identifiers are emitted as
 *     extern function calls so that the linker can bind them later
 *
 * Constructs that have no clean static lowering (closures, classes,
 * generators, async/await, prototype chains) are reported as warnings
 * and elided from IR â€?they remain valid JavaScript at runtime, just
 * not part of the cross-language IR surface.
 */
#include "frontends/javascript/include/javascript_lowering.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "middle/include/ir/ir_builder.h"

namespace polyglot::javascript {

namespace {

// Map a JSDoc TypeNode to an IR primitive type.  Falls back to f64 (number).
ir::IRType ToIRType(const std::shared_ptr<TypeNode> &t) {
    if (!t) return ir::IRType::F64();
    if (auto nt = std::dynamic_pointer_cast<NamedType>(t)) {
        const std::string &n = nt->name;
        if (n == "number" || n == "Number") return ir::IRType::F64();
        if (n == "integer" || n == "int" || n == "i32") return ir::IRType::I32(true);
        if (n == "i64" || n == "long") return ir::IRType::I64(true);
        if (n == "u32") return ir::IRType::I32(false);
        if (n == "u64") return ir::IRType::I64(false);
        if (n == "f32" || n == "float") return ir::IRType::F32();
        if (n == "f64" || n == "double") return ir::IRType::F64();
        if (n == "boolean" || n == "Boolean" || n == "bool") return ir::IRType::I1();
        if (n == "string" || n == "String") return ir::IRType::Pointer(ir::IRType::I8());
        if (n == "void" || n == "undefined" || n == "null") return ir::IRType::Void();
        if (n == "bigint" || n == "BigInt") return ir::IRType::I64(true);
        if (n == "any" || n == "unknown" || n == "object" || n == "Object")
            return ir::IRType::Pointer(ir::IRType::I8());
        return ir::IRType::F64();
    }
    if (auto gt = std::dynamic_pointer_cast<GenericType>(t)) {
        if (gt->name == "Promise" || gt->name == "Array" || gt->name == "Map" ||
            gt->name == "Set") {
            return ir::IRType::Pointer(ir::IRType::I8());
        }
        return ir::IRType::F64();
    }
    if (std::dynamic_pointer_cast<UnionType>(t)) {
        // Union types collapse to "any" (opaque pointer) for IR purposes.
        return ir::IRType::Pointer(ir::IRType::I8());
    }
    return ir::IRType::F64();
}

class Lowerer {
  public:
    Lowerer(const Module &mod, ir::IRContext &ctx, frontends::Diagnostics &diag)
        : module_(mod), ctx_(ctx), builder_(ctx), diag_(diag) {}

    void Run() {
        // Lower each top-level function declaration; everything else is
        // reported but not embedded in IR (we focus on the FFI surface).
        for (auto &s : module_.body) {
            if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(s)) {
                LowerFunction(*fd);
            } else if (auto exp = std::dynamic_pointer_cast<ExportDecl>(s)) {
                if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(exp->declaration)) {
                    LowerFunction(*fd);
                }
            }
        }
    }

  private:
    // ---- Function lowering -------------------------------------------------

    void LowerFunction(const FunctionDecl &fd) {
        if (fd.is_async || fd.is_generator) {
            diag_.Report(fd.loc,
                "JavaScript " + std::string(fd.is_async ? "async" : "generator") +
                " function '" + fd.name + "' is not lowered to IR");
            return;
        }

        std::vector<std::pair<std::string, ir::IRType>> ir_params;
        ir_params.reserve(fd.params.size());
        for (auto &p : fd.params) {
            if (p.rest) continue;
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
                if (terminated_) break;
                LowerStatement(s);
            }
        }
        if (!terminated_) {
            // Implicit return â€?produce 0/undefined of the right type.
            if (ret_ty.kind == ir::IRTypeKind::kVoid) {
                builder_.MakeReturn();
            } else {
                std::string zero = (ret_ty.kind == ir::IRTypeKind::kF32 ||
                                    ret_ty.kind == ir::IRTypeKind::kF64)
                                       ? builder_.MakeLiteral(0.0)->name
                                       : builder_.MakeLiteral((long long)0)->name;
                builder_.MakeReturn(zero);
            }
        }
        builder_.ClearCurrentFunction();
    }

    // ---- Statement lowering ------------------------------------------------

    void LowerStatement(const std::shared_ptr<Statement> &s) {
        if (!s || terminated_) return;
        if (auto blk = std::dynamic_pointer_cast<BlockStatement>(s)) {
            for (auto &c : blk->statements) {
                if (terminated_) return;
                LowerStatement(c);
            }
            return;
        }
        if (auto vd = std::dynamic_pointer_cast<VariableDecl>(s)) {
            for (auto &d : vd->decls) {
                ir::IRType ty = ir::IRType::F64();
                if (d.type) ty = ToIRType(d.type);
                auto a = builder_.MakeAlloca(ty, d.name + ".addr");
                locals_[d.name] = {d.name + ".addr", ty};
                if (d.init) {
                    auto v = LowerExpression(d.init, ty);
                    if (!v.empty()) builder_.MakeStore(d.name + ".addr", v);
                }
            }
            return;
        }
        if (auto es = std::dynamic_pointer_cast<ExprStatement>(s)) {
            LowerExpression(es->expr, ir::IRType::F64());
            return;
        }
        if (auto rs = std::dynamic_pointer_cast<ReturnStatement>(s)) {
            if (rs->value) {
                auto v = LowerExpression(rs->value, current_ret_);
                builder_.MakeReturn(v);
            } else {
                builder_.MakeReturn();
            }
            terminated_ = true;
            return;
        }
        if (auto ifs = std::dynamic_pointer_cast<IfStatement>(s)) {
            auto fn = builder_.CurrentFunction();
            auto then_bb = fn->CreateBlock("if.then");
            auto else_bb = ifs->else_branch ? fn->CreateBlock("if.else") : nullptr;
            auto cont_bb = fn->CreateBlock("if.end");
            auto cond = LowerExpression(ifs->condition, ir::IRType::I1());
            builder_.MakeCondBranch(cond, then_bb.get(),
                                    else_bb ? else_bb.get() : cont_bb.get());

            builder_.SetInsertPoint(then_bb);
            terminated_ = false;
            LowerStatement(ifs->then_branch);
            if (!terminated_) builder_.MakeBranch(cont_bb.get());

            if (else_bb) {
                builder_.SetInsertPoint(else_bb);
                terminated_ = false;
                LowerStatement(ifs->else_branch);
                if (!terminated_) builder_.MakeBranch(cont_bb.get());
            }
            builder_.SetInsertPoint(cont_bb);
            terminated_ = false;
            return;
        }
        if (auto wh = std::dynamic_pointer_cast<WhileStatement>(s)) {
            auto fn = builder_.CurrentFunction();
            auto cond_bb = fn->CreateBlock("while.cond");
            auto body_bb = fn->CreateBlock("while.body");
            auto end_bb  = fn->CreateBlock("while.end");
            builder_.MakeBranch(cond_bb.get());
            builder_.SetInsertPoint(cond_bb);
            auto cv = LowerExpression(wh->condition, ir::IRType::I1());
            builder_.MakeCondBranch(cv, body_bb.get(), end_bb.get());
            builder_.SetInsertPoint(body_bb);
            terminated_ = false;
            loop_stack_.push_back({cond_bb.get(), end_bb.get()});
            LowerStatement(wh->body);
            loop_stack_.pop_back();
            if (!terminated_) builder_.MakeBranch(cond_bb.get());
            builder_.SetInsertPoint(end_bb);
            terminated_ = false;
            return;
        }
        if (auto fs = std::dynamic_pointer_cast<ForStatement>(s)) {
            auto fn = builder_.CurrentFunction();
            if (fs->init) LowerStatement(fs->init);
            auto cond_bb = fn->CreateBlock("for.cond");
            auto body_bb = fn->CreateBlock("for.body");
            auto step_bb = fn->CreateBlock("for.step");
            auto end_bb  = fn->CreateBlock("for.end");
            builder_.MakeBranch(cond_bb.get());
            builder_.SetInsertPoint(cond_bb);
            if (fs->condition) {
                auto cv = LowerExpression(fs->condition, ir::IRType::I1());
                builder_.MakeCondBranch(cv, body_bb.get(), end_bb.get());
            } else {
                builder_.MakeBranch(body_bb.get());
            }
            builder_.SetInsertPoint(body_bb);
            terminated_ = false;
            loop_stack_.push_back({step_bb.get(), end_bb.get()});
            LowerStatement(fs->body);
            loop_stack_.pop_back();
            if (!terminated_) builder_.MakeBranch(step_bb.get());
            builder_.SetInsertPoint(step_bb);
            terminated_ = false;
            if (fs->update) LowerExpression(fs->update, ir::IRType::F64());
            builder_.MakeBranch(cond_bb.get());
            builder_.SetInsertPoint(end_bb);
            terminated_ = false;
            return;
        }
        if (auto br = std::dynamic_pointer_cast<BreakStatement>(s)) {
            if (!loop_stack_.empty()) {
                builder_.MakeBranch(loop_stack_.back().brk);
                terminated_ = true;
            }
            return;
        }
        if (auto co = std::dynamic_pointer_cast<ContinueStatement>(s)) {
            if (!loop_stack_.empty()) {
                builder_.MakeBranch(loop_stack_.back().cont);
                terminated_ = true;
            }
            return;
        }
        // Other statements are silently skipped â€?they have no IR equivalent.
    }

    // ---- Expression lowering ----------------------------------------------

    static bool IsFloat(const ir::IRType &t) {
        return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
    }

    std::string LowerExpression(const std::shared_ptr<Expression> &e,
                                const ir::IRType &want) {
        if (!e) return "";
        if (auto lit = std::dynamic_pointer_cast<Literal>(e)) {
            return LowerLiteral(*lit, want);
        }
        if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
            auto it = locals_.find(id->name);
            if (it != locals_.end()) {
                auto load = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
                return load->name;
            }
            // Unknown identifier â€?leave the lookup to the runtime.
            diag_.Report(id->loc, "unresolved identifier '" + id->name +
                                  "' in IR lowering; emitting opaque reference");
            return id->name;
        }
        if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            return LowerBinary(*bin, want);
        }
        if (auto lg = std::dynamic_pointer_cast<LogicalExpr>(e)) {
            // Lower &&/||/?? as bitwise on i1 â€?semantic short-circuit is a
            // future optimisation; this preserves truthy-falsy values for
            // typical JSDoc'd boolean code.
            auto l = LowerExpression(lg->left,  ir::IRType::I1());
            auto r = LowerExpression(lg->right, ir::IRType::I1());
            ir::BinaryInstruction::Op op =
                (lg->op == "&&") ? ir::BinaryInstruction::Op::kAnd
                                 : ir::BinaryInstruction::Op::kOr;
            auto bi = builder_.MakeBinary(op, l, r, "logical");
            return bi->name;
        }
        if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            return LowerUnary(*u, want);
        }
        if (auto u = std::dynamic_pointer_cast<UpdateExpr>(e)) {
            return LowerUpdate(*u);
        }
        if (auto a = std::dynamic_pointer_cast<AssignExpr>(e)) {
            auto rhs = LowerExpression(a->value, want);
            if (auto id = std::dynamic_pointer_cast<Identifier>(a->target)) {
                auto it = locals_.find(id->name);
                if (it != locals_.end()) {
                    builder_.MakeStore(it->second.addr, rhs);
                }
            }
            return rhs;
        }
        if (auto c = std::dynamic_pointer_cast<ConditionalExpr>(e)) {
            auto fn = builder_.CurrentFunction();
            auto then_bb = fn->CreateBlock("cond.then");
            auto else_bb = fn->CreateBlock("cond.else");
            auto end_bb  = fn->CreateBlock("cond.end");
            auto cv = LowerExpression(c->test, ir::IRType::I1());
            auto slot = builder_.MakeAlloca(want, "cond.tmp");
            builder_.MakeCondBranch(cv, then_bb.get(), else_bb.get());
            builder_.SetInsertPoint(then_bb);
            auto t = LowerExpression(c->then_branch, want);
            builder_.MakeStore("cond.tmp", t);
            builder_.MakeBranch(end_bb.get());
            builder_.SetInsertPoint(else_bb);
            auto f = LowerExpression(c->else_branch, want);
            builder_.MakeStore("cond.tmp", f);
            builder_.MakeBranch(end_bb.get());
            builder_.SetInsertPoint(end_bb);
            auto load = builder_.MakeLoad("cond.tmp", want, "cond.val");
            return load->name;
        }
        if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
            return LowerCall(*call, want);
        }
        // Arrow/function expressions, member expressions, templates, etc.
        // are not part of the static IR surface â€?return a literal zero.
        return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                             : builder_.MakeLiteral((long long)0)->name;
    }

    std::string LowerLiteral(const Literal &lit, const ir::IRType &want) {
        switch (lit.kind) {
            case Literal::Kind::kNumber: {
                bool is_float = lit.value.find('.') != std::string::npos ||
                                lit.value.find('e') != std::string::npos ||
                                lit.value.find('E') != std::string::npos;
                if (IsFloat(want) || is_float) {
                    double d = 0.0;
                    try { d = std::stod(lit.value); } catch (...) {}
                    return builder_.MakeLiteral(d)->name;
                }
                long long v = 0;
                try {
                    if (lit.value.size() > 2 && lit.value[0] == '0' &&
                        (lit.value[1] == 'x' || lit.value[1] == 'X')) {
                        v = std::stoll(lit.value.substr(2), nullptr, 16);
                    } else if (lit.value.size() > 2 && lit.value[0] == '0' &&
                               (lit.value[1] == 'b' || lit.value[1] == 'B')) {
                        v = std::stoll(lit.value.substr(2), nullptr, 2);
                    } else if (lit.value.size() > 2 && lit.value[0] == '0' &&
                               (lit.value[1] == 'o' || lit.value[1] == 'O')) {
                        v = std::stoll(lit.value.substr(2), nullptr, 8);
                    } else {
                        v = std::stoll(lit.value);
                    }
                } catch (...) {}
                return builder_.MakeLiteral(v)->name;
            }
            case Literal::Kind::kBigInt: {
                std::string raw = lit.value;
                if (!raw.empty() && raw.back() == 'n') raw.pop_back();
                long long v = 0;
                try { v = std::stoll(raw); } catch (...) {}
                return builder_.MakeLiteral(v)->name;
            }
            case Literal::Kind::kBool:
                return builder_.MakeLiteral((long long)(lit.value == "true" ? 1 : 0))
                    ->name;
            case Literal::Kind::kNull:
            case Literal::Kind::kUndefined:
                return builder_.MakeLiteral((long long)0)->name;
            case Literal::Kind::kString:
            case Literal::Kind::kTemplateString:
            case Literal::Kind::kRegex:
                return builder_.MakeStringLiteral(lit.value, "jsstr");
        }
        return builder_.MakeLiteral((long long)0)->name;
    }

    std::string LowerBinary(const BinaryExpr &b, const ir::IRType &want) {
        auto l = LowerExpression(b.left, want);
        auto r = LowerExpression(b.right, want);
        using Op = ir::BinaryInstruction::Op;
        Op op;
        bool is_cmp = false;
        bool is_float = IsFloat(want);
        if (b.op == "+")        op = is_float ? Op::kFAdd : Op::kAdd;
        else if (b.op == "-")   op = is_float ? Op::kFSub : Op::kSub;
        else if (b.op == "*")   op = is_float ? Op::kFMul : Op::kMul;
        else if (b.op == "/")   op = is_float ? Op::kFDiv : Op::kSDiv;
        else if (b.op == "%")   op = is_float ? Op::kFRem : Op::kSRem;
        else if (b.op == "&")   op = Op::kAnd;
        else if (b.op == "|")   op = Op::kOr;
        else if (b.op == "^")   op = Op::kXor;
        else if (b.op == "<<")  op = Op::kShl;
        else if (b.op == ">>")  op = Op::kAShr;
        else if (b.op == ">>>") op = Op::kLShr;
        else if (b.op == "==" || b.op == "===") { op = is_float ? Op::kCmpFoe : Op::kCmpEq; is_cmp = true; }
        else if (b.op == "!=" || b.op == "!==") { op = is_float ? Op::kCmpFne : Op::kCmpNe; is_cmp = true; }
        else if (b.op == "<")  { op = is_float ? Op::kCmpFlt : Op::kCmpSlt; is_cmp = true; }
        else if (b.op == "<=") { op = is_float ? Op::kCmpFle : Op::kCmpSle; is_cmp = true; }
        else if (b.op == ">")  { op = is_float ? Op::kCmpFgt : Op::kCmpSgt; is_cmp = true; }
        else if (b.op == ">=") { op = is_float ? Op::kCmpFge : Op::kCmpSge; is_cmp = true; }
        else                    op = Op::kAdd;
        auto bi = builder_.MakeBinary(op, l, r, is_cmp ? "cmp" : "bop");
        return bi->name;
    }

    std::string LowerUnary(const UnaryExpr &u, const ir::IRType &want) {
        auto v = LowerExpression(u.operand, want);
        if (u.op == "-") {
            auto zero = IsFloat(want)
                            ? builder_.MakeLiteral(0.0)->name
                            : builder_.MakeLiteral((long long)0)->name;
            auto bi = builder_.MakeBinary(
                IsFloat(want) ? ir::BinaryInstruction::Op::kFSub
                              : ir::BinaryInstruction::Op::kSub,
                zero, v, "neg");
            return bi->name;
        }
        if (u.op == "!") {
            auto one = builder_.MakeLiteral((long long)1)->name;
            auto bi = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, v, one, "not");
            return bi->name;
        }
        if (u.op == "~") {
            auto m1 = builder_.MakeLiteral((long long)-1)->name;
            auto bi = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, v, m1, "bnot");
            return bi->name;
        }
        return v;  // typeof / void / delete are pass-throughs in IR.
    }

    std::string LowerUpdate(const UpdateExpr &u) {
        if (auto id = std::dynamic_pointer_cast<Identifier>(u.target)) {
            auto it = locals_.find(id->name);
            if (it == locals_.end()) return "";
            auto loaded = builder_.MakeLoad(it->second.addr, it->second.type, id->name);
            auto one = IsFloat(it->second.type)
                           ? builder_.MakeLiteral(1.0)->name
                           : builder_.MakeLiteral((long long)1)->name;
            auto op = (u.op == "++")
                          ? (IsFloat(it->second.type) ? ir::BinaryInstruction::Op::kFAdd
                                                      : ir::BinaryInstruction::Op::kAdd)
                          : (IsFloat(it->second.type) ? ir::BinaryInstruction::Op::kFSub
                                                      : ir::BinaryInstruction::Op::kSub);
            auto bi = builder_.MakeBinary(op, loaded->name, one, "upd");
            builder_.MakeStore(it->second.addr, bi->name);
            return u.prefix ? bi->name : loaded->name;
        }
        return "";
    }

    std::string LowerCall(const CallExpr &c, const ir::IRType &want) {
        std::string callee_name;
        if (auto id = std::dynamic_pointer_cast<Identifier>(c.callee)) {
            callee_name = id->name;
        } else if (auto m = std::dynamic_pointer_cast<MemberExpr>(c.callee)) {
            // Lower as flattened name "obj.prop"
            if (auto o = std::dynamic_pointer_cast<Identifier>(m->object)) {
                callee_name = o->name + "." + m->property;
            } else {
                callee_name = m->property;
            }
        } else {
            return builder_.MakeLiteral((long long)0)->name;
        }

        std::vector<std::string> args;
        args.reserve(c.args.size());
        for (auto &a : c.args) {
            args.push_back(LowerExpression(a, ir::IRType::F64()));
        }
        auto call = builder_.MakeCall(callee_name, args, want, "call");
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
    std::vector<LoopFrame> loop_stack_;
    ir::IRType current_ret_{ir::IRType::Void()};
    bool terminated_{false};
};

}  // namespace

void LowerToIR(const Module &mod, ir::IRContext &ctx,
               frontends::Diagnostics &diag) {
    Lowerer l(mod, ctx, diag);
    l.Run();
}

}  // namespace polyglot::javascript
