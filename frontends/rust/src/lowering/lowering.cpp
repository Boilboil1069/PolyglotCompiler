#include "frontends/rust/include/rust_lowering.h"
#include "frontends/rust/include/rust_ast.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

#include "common/include/core/types.h"
#include "common/include/ir/ir_builder.h"
#include "common/include/ir/ir_printer.h"

namespace polyglot::rust {
namespace {

using Name = std::string;

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &type) {
    if (!type) return ir::IRType::I64(true);
    
    if (auto path = std::dynamic_pointer_cast<TypePath>(type)) {
        std::string type_name = path->segments.empty() ? "" : path->segments.back();
        if (type_name == "i32" || type_name == "i64") return ir::IRType::I64(true);
        if (type_name == "u32" || type_name == "u64") return ir::IRType::I64(false);
        if (type_name == "f32") return ir::IRType::F32();
        if (type_name == "f64") return ir::IRType::F64();
        if (type_name == "bool") return ir::IRType::I1();
        if (path->segments.empty()) return ir::IRType::Void();
    }
    
    return ir::IRType::I64(true);
}

struct EnvEntry {
    Name value;
    ir::IRType type{ir::IRType::Invalid()};
};

struct LoweringContext {
    ir::IRContext &ir_ctx;
    frontends::Diagnostics &diags;
    std::unordered_map<Name, EnvEntry> env;
    ir::IRBuilder builder;
    std::shared_ptr<ir::Function> fn;
    bool terminated{false};

    LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d)
        : ir_ctx(ctx), diags(d), builder(ctx) {}
};

struct EvalResult {
    Name value;
    ir::IRType type{ir::IRType::Invalid()};
};

bool IsIntegerLiteral(const std::string &text, long long *out) {
    char *end = nullptr;
    long long v = std::strtoll(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0') return false;
    if (out) *out = v;
    return true;
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc);

EvalResult MakeLiteral(long long v, LoweringContext &lc) {
    (void)lc;
    return {std::to_string(v), ir::IRType::I64(true)};
}

EvalResult EvalPath(const std::shared_ptr<PathExpression> &path, LoweringContext &lc) {
    std::string path_name = path->segments.empty() ? "" : path->segments.back();
    auto it = lc.env.find(path_name);
    if (it == lc.env.end()) {
        lc.diags.Report(path->loc, "Undefined path: " + path_name);
        return {};
    }
    return {it->second.value, it->second.type};
}

ir::BinaryInstruction::Op MapBinOp(const std::string &op) {
    if (op == "+") return ir::BinaryInstruction::Op::kAdd;
    if (op == "-") return ir::BinaryInstruction::Op::kSub;
    if (op == "*") return ir::BinaryInstruction::Op::kMul;
    if (op == "/") return ir::BinaryInstruction::Op::kSDiv;
    if (op == "%") return ir::BinaryInstruction::Op::kSRem;
    if (op == "==") return ir::BinaryInstruction::Op::kCmpEq;
    if (op == "!=") return ir::BinaryInstruction::Op::kCmpNe;
    if (op == "<") return ir::BinaryInstruction::Op::kCmpSlt;
    if (op == "<=") return ir::BinaryInstruction::Op::kCmpSle;
    if (op == ">") return ir::BinaryInstruction::Op::kCmpSgt;
    if (op == ">=") return ir::BinaryInstruction::Op::kCmpSge;
    return ir::BinaryInstruction::Op::kAdd;
}

EvalResult EvalBinary(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
    auto lhs = EvalExpr(bin->left, lc);
    auto rhs = EvalExpr(bin->right, lc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid || rhs.type.kind == ir::IRTypeKind::kInvalid)
        return {};
    
    ir::BinaryInstruction::Op op = MapBinOp(bin->op);
    auto inst = lc.builder.MakeBinary(op, lhs.value, rhs.value, "");
    
    switch (op) {
        case ir::BinaryInstruction::Op::kCmpEq:
        case ir::BinaryInstruction::Op::kCmpNe:
        case ir::BinaryInstruction::Op::kCmpUlt:
        case ir::BinaryInstruction::Op::kCmpUle:
        case ir::BinaryInstruction::Op::kCmpUgt:
        case ir::BinaryInstruction::Op::kCmpUge:
        case ir::BinaryInstruction::Op::kCmpSlt:
        case ir::BinaryInstruction::Op::kCmpSle:
        case ir::BinaryInstruction::Op::kCmpSgt:
        case ir::BinaryInstruction::Op::kCmpSge:
            inst->type = ir::IRType::I1();
            break;
        default:
            inst->type = lhs.type;
            break;
    }
    return {inst->name, inst->type};
}

EvalResult EvalCall(const std::shared_ptr<CallExpression> &call, LoweringContext &lc) {
    std::vector<std::string> args;
    std::vector<ir::IRType> arg_types;
    for (const auto &arg : call->args) {
        auto ev = EvalExpr(arg, lc);
        if (ev.type.kind == ir::IRTypeKind::kInvalid) return {};
        args.push_back(ev.value);
        arg_types.push_back(ev.type);
    }
    
    std::string callee_name;
    if (auto path = std::dynamic_pointer_cast<PathExpression>(call->callee)) {
        callee_name = path->segments.empty() ? "" : path->segments.back();
    } else {
        lc.diags.Report(call->loc, "Only direct function calls are supported");
        return {};
    }
    
    auto inst = lc.builder.MakeCall(callee_name, args, ir::IRType::I64(true), "");
    return {inst->name, inst->type};
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
    if (!expr) return {};
    
    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        long long v{};
        if (!IsIntegerLiteral(lit->value, &v)) {
            lc.diags.Report(lit->loc, "Invalid integer literal");
            return {};
        }
        return MakeLiteral(v, lc);
    }
    
    if (auto path = std::dynamic_pointer_cast<PathExpression>(expr)) {
        return EvalPath(path, lc);
    }
    
    if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return EvalBinary(bin, lc);
    }
    
    if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
        return EvalCall(call, lc);
    }
    
    lc.diags.Report(expr->loc, "Unsupported expression in lowering");
    return {};
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);

bool LowerReturn(const std::shared_ptr<ReturnStatement> &ret, LoweringContext &lc) {
    if (lc.terminated) return true;
    EvalResult v;
    if (ret->value) v = EvalExpr(ret->value, lc);
    lc.builder.MakeReturn(v.value);
    lc.terminated = true;
    return true;
}

bool LowerLet(const std::shared_ptr<LetStatement> &let, LoweringContext &lc) {
    if (!let->init) {
        lc.diags.Report(let->loc, "Let binding requires initializer");
        return false;
    }
    
    auto result = EvalExpr(let->init, lc);
    if (result.type.kind == ir::IRTypeKind::kInvalid) return false;
    
    if (auto pat = std::dynamic_pointer_cast<IdentifierPattern>(let->pattern)) {
        lc.env[pat->name] = {result.value, result.type};
        return true;
    }
    
    lc.diags.Report(let->loc, "Only simple identifier patterns supported");
    return false;
}

bool LowerIf(const std::shared_ptr<IfExpression> &if_expr, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    auto cond = EvalExpr(if_expr->condition, lc);
    if (cond.type.kind == ir::IRTypeKind::kInvalid) return false;
    
    auto *then_block = lc.fn->CreateBlock("if.then");
    auto *else_block = if_expr->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
    auto *merge_block = lc.fn->CreateBlock("if.end");
    
    lc.builder.MakeCondBranch(cond.value, then_block,
                              else_block ? else_block : merge_block);
    lc.terminated = false;
    
    // Then block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == then_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    bool then_term = false;
    for (auto &s : if_expr->then_body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) {
            then_term = true;
            break;
        }
    }
    if (!then_term) {
        lc.builder.MakeBranch(merge_block);
    }
    
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
        for (auto &s : if_expr->else_body) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) {
                else_term = true;
                break;
            }
        }
        if (!else_term) {
            lc.builder.MakeBranch(merge_block);
        }
    }
    
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == merge_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = then_term && (else_term || !else_block);
    return true;
}

bool LowerLoop(const std::shared_ptr<LoopStatement> &loop, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    auto *body_block = lc.fn->CreateBlock("loop.body");
    auto *exit_block = lc.fn->CreateBlock("loop.exit");
    
    lc.builder.MakeBranch(body_block);
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == body_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    
    for (auto &s : loop->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }
    
    if (!lc.terminated) {
        lc.builder.MakeBranch(body_block);
    }
    
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == exit_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    return true;
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
    if (!stmt || lc.terminated) return true;
    
    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) return LowerReturn(ret, lc);
    if (auto let = std::dynamic_pointer_cast<LetStatement>(stmt)) return LowerLet(let, lc);
    if (auto loop = std::dynamic_pointer_cast<LoopStatement>(stmt)) return LowerLoop(loop, lc);
    
    if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        if (auto if_expr = std::dynamic_pointer_cast<IfExpression>(expr_stmt->expr)) {
            return LowerIf(if_expr, lc);
        }
        (void)EvalExpr(expr_stmt->expr, lc);
        return true;
    }
    
    lc.diags.Report(stmt->loc, "Unsupported statement in lowering");
    return false;
}

bool LowerFunction(const FunctionItem &fn, LoweringContext &lc) {
    ir::IRType ret_ty = ToIRType(fn.return_type);
    if (ret_ty.kind == ir::IRTypeKind::kInvalid) ret_ty = ir::IRType::Void();
    
    std::vector<std::pair<std::string, ir::IRType>> params;
    params.reserve(fn.params.size());
    for (auto &param : fn.params) {
        ir::IRType param_ty = ToIRType(param.type);
        if (param_ty.kind == ir::IRTypeKind::kInvalid) {
            lc.diags.Report(param.type ? param.type->loc : fn.loc, "Unsupported parameter type");
            return false;
        }
        params.push_back({param.name, param_ty});
    }
    
    lc.fn = lc.ir_ctx.CreateFunction(fn.name, ret_ty, params);
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == entry) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    
    lc.env.clear();
    for (const auto &p : params) {
        lc.env[p.first] = {p.first, p.second};
    }
    lc.terminated = false;
    
    for (auto &stmt : fn.body) {
        if (!LowerStmt(stmt, lc)) return false;
        if (lc.terminated) break;
    }
    
    if (!lc.terminated) {
        if (ret_ty.kind == ir::IRTypeKind::kVoid) {
            lc.builder.MakeReturn("");
        } else {
            auto zero = MakeLiteral(0, lc);
            lc.builder.MakeReturn(zero.value);
        }
    }
    return true;
}

} // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
    LoweringContext lc(ctx, diags);
    for (const auto &item : module.items) {
        if (auto fn = std::dynamic_pointer_cast<FunctionItem>(item)) {
            LowerFunction(*fn, lc);
        }
    }
}

} // namespace polyglot::rust
