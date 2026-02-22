#include "frontends/dotnet/include/dotnet_lowering.h"

#include <cstdlib>
#include <string>
#include <unordered_map>

#include "common/include/core/types.h"
#include "middle/include/ir/ir_builder.h"

namespace polyglot::dotnet {
namespace {

using Name = std::string;

ir::IRType ToIRType(const core::Type &t) {
    using Kind = core::TypeKind;
    switch (t.kind) {
        case Kind::kInt:   return ir::IRType::I64(true);
        case Kind::kFloat: return ir::IRType::F64();
        case Kind::kBool:  return ir::IRType::I1();
        case Kind::kVoid:  return ir::IRType::Void();
        case Kind::kString:
        case Kind::kPointer:
            return ir::IRType::Pointer(ir::IRType::I8());
        default:
            return ir::IRType::Invalid();
    }
}

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &node) {
    if (!node) return ir::IRType::I64(true);
    if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
        auto &n = simple->name;
        if (n == "sbyte")   return ir::IRType::I8(true);
        if (n == "byte")    return ir::IRType::I8(false);
        if (n == "short")   return ir::IRType::I16(true);
        if (n == "ushort")  return ir::IRType::I16(false);
        if (n == "int")     return ir::IRType::I32(true);
        if (n == "uint")    return ir::IRType::I32(false);
        if (n == "long")    return ir::IRType::I64(true);
        if (n == "ulong")   return ir::IRType::I64(false);
        if (n == "nint")    return ir::IRType::I64(true);
        if (n == "nuint")   return ir::IRType::I64(false);
        if (n == "float")   return ir::IRType::F32();
        if (n == "double")  return ir::IRType::F64();
        if (n == "decimal") return ir::IRType::F64(); // simplified
        if (n == "bool")    return ir::IRType::I1();
        if (n == "char")    return ir::IRType::I16(false);
        if (n == "void")    return ir::IRType::Void();
        if (n == "string" || n == "System.String" || n == "object" || n == "System.Object")
            return ir::IRType::Pointer(ir::IRType::I8());
        return ir::IRType::Pointer(ir::IRType::I8()); // reference types
    }
    if (auto arr = std::dynamic_pointer_cast<ArrayType>(node)) {
        return ir::IRType::Pointer(ToIRType(arr->element_type));
    }
    if (auto nullable = std::dynamic_pointer_cast<NullableType>(node)) {
        return ToIRType(nullable->inner);
    }
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
    std::string current_class;
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

bool IsFloatLiteral(const std::string &text, double *out) {
    char *end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || (*end != '\0' && *end != 'f' && *end != 'F' &&
                                  *end != 'd' && *end != 'D' &&
                                  *end != 'm' && *end != 'M')) return false;
    if (out) *out = v;
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

ir::BinaryInstruction::Op MapBinOp(const std::string &op, bool is_float) {
    if (op == "+") return is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
    if (op == "-") return is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
    if (op == "*") return is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
    if (op == "/") return is_float ? ir::BinaryInstruction::Op::kFDiv : ir::BinaryInstruction::Op::kSDiv;
    if (op == "%") return ir::BinaryInstruction::Op::kSRem;
    if (op == "&") return ir::BinaryInstruction::Op::kAnd;
    if (op == "|") return ir::BinaryInstruction::Op::kOr;
    if (op == "^") return ir::BinaryInstruction::Op::kXor;
    if (op == "<<") return ir::BinaryInstruction::Op::kShl;
    if (op == ">>") return ir::BinaryInstruction::Op::kAShr;
    if (op == ">>>") return ir::BinaryInstruction::Op::kLShr;
    if (op == "==") return is_float ? ir::BinaryInstruction::Op::kCmpFoe : ir::BinaryInstruction::Op::kCmpEq;
    if (op == "!=") return is_float ? ir::BinaryInstruction::Op::kCmpFne : ir::BinaryInstruction::Op::kCmpNe;
    if (op == "<") return is_float ? ir::BinaryInstruction::Op::kCmpFlt : ir::BinaryInstruction::Op::kCmpSlt;
    if (op == "<=") return is_float ? ir::BinaryInstruction::Op::kCmpFle : ir::BinaryInstruction::Op::kCmpSle;
    if (op == ">") return is_float ? ir::BinaryInstruction::Op::kCmpFgt : ir::BinaryInstruction::Op::kCmpSgt;
    if (op == ">=") return is_float ? ir::BinaryInstruction::Op::kCmpFge : ir::BinaryInstruction::Op::kCmpSge;
    return ir::BinaryInstruction::Op::kAdd;
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

// ==================== Expression evaluation ====================

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
    if (!expr) return {};

    if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
        auto it = lc.env.find(id->name);
        if (it != lc.env.end()) return {it->second.value, it->second.type};
        return {id->name, ir::IRType::I64(true)};
    }

    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        auto &v = lit->value;
        if (v == "true")  return {"1", ir::IRType::I1()};
        if (v == "false") return {"0", ir::IRType::I1()};
        if (v == "null")  return {"0", ir::IRType::Pointer(ir::IRType::I8())};

        if (!v.empty() && v[0] == '"') {
            auto name = lc.builder.MakeStringLiteral(v, "csstr");
            return {name, ir::IRType::Pointer(ir::IRType::I8())};
        }

        long long iv;
        if (IsIntegerLiteral(v, &iv)) return MakeLiteral(iv, lc);

        double fv;
        if (IsFloatLiteral(v, &fv)) return MakeFloatLiteral(fv, lc);

        if (!v.empty() && v[0] == '\'') {
            if (v.size() >= 3) return MakeLiteral(static_cast<long long>(v[1]), lc);
            return MakeLiteral(0, lc);
        }

        return {v, ir::IRType::I64(true)};
    }

    if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
        auto operand = EvalExpr(unary->operand, lc);
        if (operand.type.kind == ir::IRTypeKind::kInvalid) return {};
        if (unary->op == "-") {
            auto neg = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kSub,
                                              "0", operand.value, "");
            return {neg->name, operand.type};
        }
        if (unary->op == "!") {
            auto not_val = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor,
                                                  operand.value, "1", "");
            return {not_val->name, ir::IRType::I1()};
        }
        if (unary->op == "~") {
            auto comp = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor,
                                               operand.value, "-1", "");
            return {comp->name, operand.type};
        }
        return operand;
    }

    if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        if (bin->op == "=") {
            auto rhs = EvalExpr(bin->right, lc);
            if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
                lc.env[id->name] = {rhs.value, rhs.type};
            }
            return rhs;
        }

        if (bin->op == "+=" || bin->op == "-=" || bin->op == "*=" ||
            bin->op == "/=" || bin->op == "%=") {
            auto left = EvalExpr(bin->left, lc);
            auto right = EvalExpr(bin->right, lc);
            std::string base_op = bin->op.substr(0, bin->op.size() - 1);
            bool is_float = (left.type.kind == ir::IRTypeKind::kF32 ||
                             left.type.kind == ir::IRTypeKind::kF64);
            auto op = MapBinOp(base_op, is_float);
            auto result = lc.builder.MakeBinary(op, left.value, right.value, "");
            if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
                lc.env[id->name] = {result->name, left.type};
            }
            return {result->name, left.type};
        }

        if (bin->op == "&&" || bin->op == "||") {
            auto left = EvalExpr(bin->left, lc);
            auto right = EvalExpr(bin->right, lc);
            auto op = bin->op == "&&" ? ir::BinaryInstruction::Op::kAnd
                                       : ir::BinaryInstruction::Op::kOr;
            auto logical = lc.builder.MakeBinary(op, left.value, right.value, "");
            return {logical->name, ir::IRType::I1()};
        }

        auto left = EvalExpr(bin->left, lc);
        auto right = EvalExpr(bin->right, lc);
        if (left.type.kind == ir::IRTypeKind::kInvalid ||
            right.type.kind == ir::IRTypeKind::kInvalid) return {};

        bool is_float = (left.type.kind == ir::IRTypeKind::kF32 ||
                         left.type.kind == ir::IRTypeKind::kF64);
        auto op = MapBinOp(bin->op, is_float);
        auto inst = lc.builder.MakeBinary(op, left.value, right.value, "");
        ir::IRType result_type = IsCmpOp(op) ? ir::IRType::I1() : left.type;
        return {inst->name, result_type};
    }

    if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
        std::vector<std::string> arg_values;
        for (auto &arg : call->args) {
            auto r = EvalExpr(arg, lc);
            arg_values.push_back(r.value);
        }

        std::string callee_name;
        if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
            callee_name = id->name;
        } else if (auto member = std::dynamic_pointer_cast<MemberExpression>(call->callee)) {
            auto obj = EvalExpr(member->object, lc);
            callee_name = obj.value + "." + member->member;
            arg_values.insert(arg_values.begin(), obj.value);
        }

        // Map well-known .NET methods
        if (callee_name == "Console.WriteLine" || callee_name == "Console.Write") {
            callee_name = "__ploy_dotnet_print";
        }

        auto inst = lc.builder.MakeCall(callee_name, arg_values, ir::IRType::I64(true), "");
        return {inst->name, inst->type};
    }

    if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
        auto obj = EvalExpr(member->object, lc);
        return {obj.value + "." + member->member, ir::IRType::I64(true)};
    }

    if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
        ir::IRType alloc_type = ToIRType(new_expr->type);
        auto ptr_type = ir::IRType::Pointer(alloc_type);

        std::vector<std::string> args;
        for (auto &arg : new_expr->args) {
            auto r = EvalExpr(arg, lc);
            args.push_back(r.value);
        }

        auto alloc = lc.builder.MakeCall("__builtin_new", {}, ptr_type, "");

        std::string type_name;
        if (auto st = std::dynamic_pointer_cast<SimpleType>(new_expr->type)) {
            type_name = st->name;
        }
        if (!type_name.empty()) {
            std::vector<std::string> ctor_args = {alloc->name};
            for (auto &a : args) ctor_args.push_back(a);
            lc.builder.MakeCall(type_name + "::.ctor", ctor_args, ir::IRType::Void(), "");
        }

        return {alloc->name, ptr_type};
    }

    if (auto cast = std::dynamic_pointer_cast<CastExpression>(expr)) {
        auto val = EvalExpr(cast->expr, lc);
        ir::IRType target = ToIRType(cast->target_type);
        auto conv = lc.builder.MakeCast(ir::CastInstruction::CastKind::kBitcast,
                                         val.value, target, "");
        return {conv->name, target};
    }

    if (auto index = std::dynamic_pointer_cast<IndexExpression>(expr)) {
        auto base = EvalExpr(index->object, lc);
        auto idx = EvalExpr(index->index, lc);
        ir::IRType elem_type = ir::IRType::I64(true);
        if (base.type.kind == ir::IRTypeKind::kPointer && !base.type.subtypes.empty()) {
            elem_type = base.type.subtypes[0];
        }
        auto gep = lc.builder.MakeDynamicGEP(base.value, elem_type, idx.value, "idx_ptr");
        auto load = lc.builder.MakeLoad(gep->name, elem_type, "idx_val");
        return {load->name, elem_type};
    }

    if (auto tern = std::dynamic_pointer_cast<TernaryExpression>(expr)) {
        auto cond = EvalExpr(tern->condition, lc);
        auto then_val = EvalExpr(tern->then_expr, lc);
        auto else_val = EvalExpr(tern->else_expr, lc);
        (void)cond;
        (void)else_val;
        return then_val;
    }

    if (auto nc = std::dynamic_pointer_cast<NullCoalescingExpression>(expr)) {
        auto left = EvalExpr(nc->left, lc);
        auto right = EvalExpr(nc->right, lc);
        // Simplified: null-check left, use right if null
        auto is_null = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpEq,
                                              left.value, "0", "");
        // Just return left for now (a full impl would use branches)
        (void)is_null;
        (void)right;
        return left;
    }

    if (auto is_expr = std::dynamic_pointer_cast<IsExpression>(expr)) {
        auto val = EvalExpr(is_expr->expr, lc);
        std::string type_name;
        if (auto st = std::dynamic_pointer_cast<SimpleType>(is_expr->type)) {
            type_name = st->name;
        }
        auto check = lc.builder.MakeCall("__ploy_dotnet_isinstance",
                                          {val.value, type_name}, ir::IRType::I1(), "");
        return {check->name, ir::IRType::I1()};
    }

    if (auto as_expr = std::dynamic_pointer_cast<AsExpression>(expr)) {
        auto val = EvalExpr(as_expr->expr, lc);
        ir::IRType target = ToIRType(as_expr->type);
        auto conv = lc.builder.MakeCast(ir::CastInstruction::CastKind::kBitcast,
                                         val.value, target, "");
        return {conv->name, target};
    }

    if (auto await = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
        auto val = EvalExpr(await->operand, lc);
        return val;
    }

    return {};
}

// ==================== Statement lowering ====================

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
    if (!stmt || lc.terminated) return true;

    if (auto block = std::dynamic_pointer_cast<BlockStatement>(stmt)) {
        for (auto &s : block->statements) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) break;
        }
        return true;
    }

    if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) {
        ir::IRType vt = ToIRType(var->type);
        if (var->init) {
            auto init_val = EvalExpr(var->init, lc);
            lc.env[var->name] = {init_val.value, init_val.type};
        } else {
            lc.env[var->name] = {"0", vt};
        }
        return true;
    }

    if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        if (expr_stmt->expr) EvalExpr(expr_stmt->expr, lc);
        return true;
    }

    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
        if (ret->value) {
            auto val = EvalExpr(ret->value, lc);
            lc.builder.MakeReturn(val.value);
        } else {
            lc.builder.MakeReturn("");
        }
        lc.terminated = true;
        return true;
    }

    if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
        auto cond = EvalExpr(if_stmt->condition, lc);
        if (cond.type.kind == ir::IRTypeKind::kInvalid) return false;

        auto *then_block = lc.fn->CreateBlock("if.then");
        auto *else_block = if_stmt->else_body ? lc.fn->CreateBlock("if.else") : nullptr;
        auto *merge_block = lc.fn->CreateBlock("if.end");

        lc.builder.MakeCondBranch(cond.value, then_block,
                                   else_block ? else_block : merge_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == then_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        LowerStmt(if_stmt->then_body, lc);
        bool then_term = lc.terminated;
        if (!then_term) lc.builder.MakeBranch(merge_block);

        bool else_term = false;
        if (else_block) {
            for (auto &bb : lc.fn->blocks) {
                if (bb.get() == else_block) { lc.builder.SetInsertPoint(bb); break; }
            }
            lc.terminated = false;
            LowerStmt(if_stmt->else_body, lc);
            else_term = lc.terminated;
            if (!else_term) lc.builder.MakeBranch(merge_block);
        }

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == merge_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = then_term && (else_term || !else_block);
        return true;
    }

    if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
        auto *cond_block = lc.fn->CreateBlock("while.cond");
        auto *body_block = lc.fn->CreateBlock("while.body");
        auto *exit_block = lc.fn->CreateBlock("while.end");

        lc.builder.MakeBranch(cond_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == cond_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        auto cond = EvalExpr(while_stmt->condition, lc);
        lc.builder.MakeCondBranch(cond.value, body_block, exit_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == body_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        LowerStmt(while_stmt->body, lc);
        if (!lc.terminated) lc.builder.MakeBranch(cond_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == exit_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        return true;
    }

    if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
        if (for_stmt->init) LowerStmt(for_stmt->init, lc);

        auto *cond_block = lc.fn->CreateBlock("for.cond");
        auto *body_block = lc.fn->CreateBlock("for.body");
        auto *inc_block  = lc.fn->CreateBlock("for.inc");
        auto *exit_block = lc.fn->CreateBlock("for.end");

        lc.builder.MakeBranch(cond_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == cond_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        if (for_stmt->condition) {
            auto cond = EvalExpr(for_stmt->condition, lc);
            lc.builder.MakeCondBranch(cond.value, body_block, exit_block);
        } else {
            lc.builder.MakeBranch(body_block);
        }

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == body_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        LowerStmt(for_stmt->body, lc);
        if (!lc.terminated) lc.builder.MakeBranch(inc_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == inc_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        if (for_stmt->update) EvalExpr(for_stmt->update, lc);
        lc.builder.MakeBranch(cond_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == exit_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        return true;
    }

    if (auto foreach_stmt = std::dynamic_pointer_cast<ForEachStatement>(stmt)) {
        auto iterable = EvalExpr(foreach_stmt->iterable, lc);
        auto iter = lc.builder.MakeCall("__ploy_dotnet_get_enumerator",
                                         {iterable.value},
                                         ir::IRType::Pointer(ir::IRType::I8()), "");

        auto *cond_block = lc.fn->CreateBlock("foreach.cond");
        auto *body_block = lc.fn->CreateBlock("foreach.body");
        auto *exit_block = lc.fn->CreateBlock("foreach.end");

        lc.builder.MakeBranch(cond_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == cond_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        auto has_next = lc.builder.MakeCall("__ploy_dotnet_move_next",
                                             {iter->name}, ir::IRType::I1(), "");
        lc.builder.MakeCondBranch(has_next->name, body_block, exit_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == body_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        ir::IRType elem_type = ToIRType(foreach_stmt->var_type);
        auto current = lc.builder.MakeCall("__ploy_dotnet_get_current",
                                            {iter->name}, elem_type, "");
        lc.env[foreach_stmt->var_name] = {current->name, elem_type};
        LowerStmt(foreach_stmt->body, lc);
        if (!lc.terminated) lc.builder.MakeBranch(cond_block);

        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == exit_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        return true;
    }

    if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
        LowerStmt(try_stmt->body, lc);
        for (auto &c : try_stmt->catches) LowerStmt(c.body, lc);
        if (try_stmt->finally_body) LowerStmt(try_stmt->finally_body, lc);
        return true;
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
        if (using_stmt->declaration) {
            if (using_stmt->declaration->init) {
                auto val = EvalExpr(using_stmt->declaration->init, lc);
                lc.env[using_stmt->declaration->name] = {val.value, val.type};
            }
        }
        if (using_stmt->body) LowerStmt(using_stmt->body, lc);
        if (using_stmt->declaration) {
            auto it = lc.env.find(using_stmt->declaration->name);
            if (it != lc.env.end()) {
                lc.builder.MakeCall("__ploy_dotnet_dispose", {it->second.value},
                                     ir::IRType::Void(), "");
            }
        }
        return true;
    }

    if (auto lock_stmt = std::dynamic_pointer_cast<LockStatement>(stmt)) {
        auto monitor = EvalExpr(lock_stmt->expr, lc);
        lc.builder.MakeCall("__ploy_dotnet_monitor_enter", {monitor.value},
                             ir::IRType::Void(), "");
        LowerStmt(lock_stmt->body, lc);
        lc.builder.MakeCall("__ploy_dotnet_monitor_exit", {monitor.value},
                             ir::IRType::Void(), "");
        return true;
    }

    if (auto switch_stmt = std::dynamic_pointer_cast<SwitchStatement>(stmt)) {
        auto sel = EvalExpr(switch_stmt->governing, lc);
        auto *end_block = lc.fn->CreateBlock("switch.end");
        for (auto &sec : switch_stmt->sections) {
            auto *sec_block = lc.fn->CreateBlock("switch.section");
            for (auto &bb : lc.fn->blocks) {
                if (bb.get() == sec_block) { lc.builder.SetInsertPoint(bb); break; }
            }
            lc.terminated = false;
            for (auto &s : sec.body) {
                if (!LowerStmt(s, lc)) return false;
                if (lc.terminated) break;
            }
            if (!lc.terminated) lc.builder.MakeBranch(end_block);
        }
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == end_block) { lc.builder.SetInsertPoint(bb); break; }
        }
        lc.terminated = false;
        return true;
    }

    return true;
}

// ==================== Declaration lowering ====================

bool LowerMethod(const MethodDecl &method, LoweringContext &lc) {
    std::string mangled = lc.current_class.empty()
                              ? method.name
                              : lc.current_class + "::" + method.name;

    ir::IRType ret = ToIRType(method.return_type);
    if (ret.kind == ir::IRTypeKind::kInvalid) ret = ir::IRType::I64(true);

    std::vector<std::pair<std::string, ir::IRType>> params;
    if (!method.is_static && !lc.current_class.empty()) {
        params.push_back({"this", ir::IRType::Pointer(ir::IRType::I8())});
    }
    for (auto &p : method.params) {
        params.push_back({p.name, ToIRType(p.type)});
    }

    lc.fn = lc.ir_ctx.CreateFunction(mangled, ret, params);
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    if (!lc.fn->blocks.empty()) {
        lc.builder.SetInsertPoint(lc.fn->blocks.back());
    }

    lc.env.clear();
    for (auto &p : params) {
        lc.env[p.first] = {p.first, p.second};
    }
    lc.terminated = false;

    // Expression body (e.g., int Foo() => 42;)
    if (method.expression_body) {
        auto val = EvalExpr(method.expression_body, lc);
        if (ret.kind != ir::IRTypeKind::kVoid) {
            lc.builder.MakeReturn(val.value);
        } else {
            lc.builder.MakeReturn("");
        }
        return true;
    }

    for (auto &s : method.body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }

    if (!lc.terminated) {
        if (ret.kind == ir::IRTypeKind::kVoid) {
            lc.builder.MakeReturn("");
        } else {
            auto zero = MakeLiteral(0, lc);
            lc.builder.MakeReturn(zero.value);
        }
    }
    return true;
}

bool LowerConstructor(const ConstructorDecl &ctor, LoweringContext &lc) {
    std::string mangled = lc.current_class + "::.ctor";

    std::vector<std::pair<std::string, ir::IRType>> params;
    params.push_back({"this", ir::IRType::Pointer(ir::IRType::I8())});
    for (auto &p : ctor.params) {
        params.push_back({p.name, ToIRType(p.type)});
    }

    lc.fn = lc.ir_ctx.CreateFunction(mangled, ir::IRType::Void(), params);
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    if (!lc.fn->blocks.empty()) {
        lc.builder.SetInsertPoint(lc.fn->blocks.back());
    }

    lc.env.clear();
    for (auto &p : params) {
        lc.env[p.first] = {p.first, p.second};
    }
    lc.terminated = false;

    // Base/this initializer call
    if (!ctor.initializer_kind.empty()) {
        std::vector<std::string> init_args = {"this"};
        for (auto &a : ctor.initializer_args) {
            auto v = EvalExpr(a, lc);
            init_args.push_back(v.value);
        }
        std::string init_name = ctor.initializer_kind == "base"
                                    ? "__base::.ctor"
                                    : lc.current_class + "::.ctor";
        lc.builder.MakeCall(init_name, init_args, ir::IRType::Void(), "");
    }

    for (auto &s : ctor.body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }

    if (!lc.terminated) {
        lc.builder.MakeReturn("");
    }
    return true;
}

void LowerClass(const ClassDecl &cls, LoweringContext &lc) {
    auto saved_class = lc.current_class;
    lc.current_class = cls.name;

    for (auto &member : cls.members) {
        if (auto method = std::dynamic_pointer_cast<MethodDecl>(member)) {
            if (!method->is_abstract && !method->is_extern) {
                LowerMethod(*method, lc);
            }
        } else if (auto ctor = std::dynamic_pointer_cast<ConstructorDecl>(member)) {
            LowerConstructor(*ctor, lc);
        } else if (auto field = std::dynamic_pointer_cast<FieldDecl>(member)) {
            if (field->is_static && field->init) {
                auto val = EvalExpr(field->init, lc);
                lc.env[cls.name + "::" + field->name] = {val.value, val.type};
            }
        } else if (auto inner = std::dynamic_pointer_cast<ClassDecl>(member)) {
            LowerClass(*inner, lc);
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
    if (!decl) return;
    if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
        LowerClass(*cls, lc);
    } else if (auto en = std::dynamic_pointer_cast<EnumDecl>(decl)) {
        LowerEnum(*en, lc);
    } else if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
        LowerNamespace(*ns, lc);
    }
}

} // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
    LoweringContext lc(ctx, diags);

    for (const auto &decl : module.declarations) {
        LowerDecl(decl, lc);
    }

    // Lower top-level statements into a synthetic Main method (C# 9.0+)
    if (!module.top_level_statements.empty()) {
        std::vector<std::pair<std::string, ir::IRType>> main_params;
        lc.fn = lc.ir_ctx.CreateFunction("<Program>$::Main",
                                          ir::IRType::I32(true), main_params);
        auto *entry = lc.fn->CreateBlock("entry");
        lc.fn->entry = entry;
        if (!lc.fn->blocks.empty()) {
            lc.builder.SetInsertPoint(lc.fn->blocks.back());
        }
        lc.env.clear();
        lc.terminated = false;

        for (const auto &stmt : module.top_level_statements) {
            if (!LowerStmt(stmt, lc)) break;
            if (lc.terminated) break;
        }

        if (!lc.terminated) {
            auto zero = MakeLiteral(0, lc);
            lc.builder.MakeReturn(zero.value);
        }
    }
}

} // namespace polyglot::dotnet
