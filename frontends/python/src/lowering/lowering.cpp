#include "frontends/python/include/python_lowering.h"
#include "frontends/python/include/python_ast.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

#include "common/include/core/types.h"
#include "common/include/ir/ir_builder.h"
#include "common/include/ir/ir_printer.h"

namespace polyglot::python {
namespace {

using Name = std::string;

ir::IRType ToIRType(const std::string &type_hint) {
    if (type_hint == "int") return ir::IRType::I64(true);
    if (type_hint == "float") return ir::IRType::F64();
    if (type_hint == "bool") return ir::IRType::I1();
    if (type_hint == "None") return ir::IRType::Void();
    // Default to i64 for dynamic types
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

EvalResult EvalName(const std::shared_ptr<Identifier> &name, LoweringContext &lc) {
    auto it = lc.env.find(name->name);
    if (it == lc.env.end()) {
        lc.diags.Report(name->loc, "Undefined name: " + name->name);
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

EvalResult EvalBinOp(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
    auto lhs = EvalExpr(bin->left, lc);
    auto rhs = EvalExpr(bin->right, lc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid || rhs.type.kind == ir::IRTypeKind::kInvalid)
        return {};
    
    ir::BinaryInstruction::Op op = MapBinOp(bin->op);
    auto inst = lc.builder.MakeBinary(op, lhs.value, rhs.value, "");
    
    // Set result type
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
        auto ev = EvalExpr(arg.value, lc);
        if (ev.type.kind == ir::IRTypeKind::kInvalid) return {};
        args.push_back(ev.value);
        arg_types.push_back(ev.type);
    }
    
    std::string callee_name;
    if (auto name = std::dynamic_pointer_cast<Identifier>(call->callee)) {
        callee_name = name->name;
    } else {
        lc.diags.Report(call->loc, "Only direct function calls are supported");
        return {};
    }
    
    auto inst = lc.builder.MakeCall(callee_name, args, ir::IRType::I64(true), "");
    return {inst->name, inst->type};
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
    if (!expr) return {};
    
    if (auto literal = std::dynamic_pointer_cast<Literal>(expr)) {
        if (!literal->is_string) {
            long long v{};
            if (!IsIntegerLiteral(literal->value, &v)) {
                lc.diags.Report(literal->loc, "Invalid integer literal");
                return {};
            }
            return MakeLiteral(v, lc);
        }
        lc.diags.Report(literal->loc, "Only integer literals supported");
        return {};
    }
    
    if (auto name = std::dynamic_pointer_cast<Identifier>(expr)) {
        return EvalName(name, lc);
    }
    
    if (auto binop = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return EvalBinOp(binop, lc);
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

bool LowerAssign(const std::shared_ptr<Assignment> &assign, LoweringContext &lc) {
    if (assign->targets.empty() || !assign->value) return false;
    
    auto result = EvalExpr(assign->value, lc);
    if (result.type.kind == ir::IRTypeKind::kInvalid) return false;
    
    // Simple assignment to single name
    if (auto name = std::dynamic_pointer_cast<Identifier>(assign->targets[0])) {
        lc.env[name->name] = {result.value, result.type};
        return true;
    }
    
    lc.diags.Report(assign->loc, "Only simple name assignments supported");
    return false;
}

bool LowerIf(const std::shared_ptr<IfStatement> &if_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    auto cond = EvalExpr(if_stmt->condition, lc);
    if (cond.type.kind == ir::IRTypeKind::kInvalid) return false;
    
    auto *then_block = lc.fn->CreateBlock("if.then");
    auto *else_block = if_stmt->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
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
    for (auto &s : if_stmt->then_body) {
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
        for (auto &s : if_stmt->else_body) {
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

bool LowerWhile(const std::shared_ptr<WhileStatement> &while_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;
    
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
    if (cond.type.kind == ir::IRTypeKind::kInvalid) return false;
    
    lc.builder.MakeCondBranch(cond.value, body_block, exit_block);
    
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == body_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    for (auto &s : while_stmt->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }
    if (!lc.terminated) {
        lc.builder.MakeBranch(cond_block);
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

bool LowerFor(const std::shared_ptr<ForStatement> &for_stmt, LoweringContext &lc) {
    // Simplified for loop (range-based only)
    lc.diags.Report(for_stmt->loc, "For loops not yet fully supported");
    return false;
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
    if (!stmt || lc.terminated) return true;
    
    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) return LowerReturn(ret, lc);
    if (auto assign = std::dynamic_pointer_cast<Assignment>(stmt)) return LowerAssign(assign, lc);
    if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) return LowerIf(if_stmt, lc);
    if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) return LowerWhile(while_stmt, lc);
    if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) return LowerFor(for_stmt, lc);
    
    if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        (void)EvalExpr(expr_stmt->expr, lc);
        return true;
    }
    
    lc.diags.Report(stmt->loc, "Unsupported statement in lowering");
    return false;
}

bool LowerFunction(const FunctionDef &fn, LoweringContext &lc) {
    // Determine return type from type hints
    ir::IRType ret_ty = ir::IRType::I64(true);
    if (fn.return_annotation) {
        // Parse return type hint if available
        if (auto id = std::dynamic_pointer_cast<Identifier>(fn.return_annotation)) {
            ret_ty = ToIRType(id->name);
        }
    }
    
    // Build parameter list
    std::vector<std::pair<std::string, ir::IRType>> params;
    params.reserve(fn.params.size());
    for (auto &arg : fn.params) {
        ir::IRType param_ty = ir::IRType::I64(true);
        if (arg.annotation) {
            if (auto id = std::dynamic_pointer_cast<Identifier>(arg.annotation)) {
                param_ty = ToIRType(id->name);
            }
        }
        params.push_back({arg.name, param_ty});
    }
    
    lc.fn = lc.ir_ctx.CreateFunction(fn.name, ret_ty, params);
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    // Find shared_ptr from blocks
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
    for (const auto &stmt : module.body) {
        auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt);
        if (!fn) continue;
        LowerFunction(*fn, lc);
    }
}

} // namespace polyglot::python
