#include "frontends/cpp/include/cpp_lowering.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

#include "common/include/core/types.h"
#include "common/include/ir/ir_builder.h"
#include "common/include/ir/ir_printer.h"

namespace polyglot::cpp {
namespace {

using Name = std::string;

ir::IRType ToIRType(const core::Type &t) {
    using Kind = core::TypeKind;
    switch (t.kind) {
        case Kind::kInt: return ir::IRType::I64(true);
        case Kind::kFloat: return ir::IRType::F64();
        case Kind::kBool: return ir::IRType::I1();
        case Kind::kVoid: return ir::IRType::Void();
        case Kind::kPointer:
            if (!t.type_args.empty()) return ir::IRType::Pointer(ToIRType(t.type_args[0]));
            return ir::IRType::Pointer(ir::IRType::Invalid());
        case Kind::kReference:
            if (!t.type_args.empty()) return ir::IRType::Reference(ToIRType(t.type_args[0]));
            return ir::IRType::Reference(ir::IRType::Invalid());
        default:
            return ir::IRType::Invalid();
    }
}

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &node) {
    if (!node) return ir::IRType::I64(true);
    if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
        core::Type ct = core::TypeSystem().MapFromLanguage("cpp", simple->name);
        return ToIRType(ct);
    }
    if (auto ptr = std::dynamic_pointer_cast<PointerType>(node)) {
        return ir::IRType::Pointer(ToIRType(ptr->pointee));
    }
    if (auto ref = std::dynamic_pointer_cast<ReferenceType>(node)) {
        return ir::IRType::Reference(ToIRType(ref->referent));
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

EvalResult EvalIdentifier(const std::shared_ptr<Identifier> &id, LoweringContext &lc) {
    auto it = lc.env.find(id->name);
    if (it == lc.env.end()) {
        lc.diags.Report(id->loc, "Undefined identifier: " + id->name);
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
    // Set result type (cmp yields i1).
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
        case ir::BinaryInstruction::Op::kCmpFoe:
        case ir::BinaryInstruction::Op::kCmpFne:
        case ir::BinaryInstruction::Op::kCmpFlt:
        case ir::BinaryInstruction::Op::kCmpFle:
        case ir::BinaryInstruction::Op::kCmpFgt:
        case ir::BinaryInstruction::Op::kCmpFge:
        case ir::BinaryInstruction::Op::kCmpLt:
            inst->type = ir::IRType::I1();
            break;
        default:
            inst->type = lhs.type;
            break;
    }
    return {inst->name, inst->type};
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
    if (!expr) return {};
    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        long long v{};
        if (!IsIntegerLiteral(lit->value, &v)) {
            lc.diags.Report(lit->loc, "Only integer literals are supported in lowering");
            return {};
        }
        return MakeLiteral(v, lc);
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
        return EvalIdentifier(id, lc);
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return EvalBinary(bin, lc);
    }
    if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
        lc.diags.Report(call->loc, "Function calls not supported in minimal lowering");
        return {};
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

bool LowerVar(const std::shared_ptr<VarDecl> &var, LoweringContext &lc) {
    EvalResult init;
    if (var->init) init = EvalExpr(var->init, lc);
    if (init.value.empty()) {
        lc.diags.Report(var->loc, "Variable initializer required in minimal lowering");
        return false;
    }
    lc.env[var->name] = {init.value, init.type};
    return true;
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
    if (!stmt || lc.terminated) return true;
    if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) return LowerVar(var, lc);
    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) return LowerReturn(ret, lc);
    if (auto expr = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        (void)EvalExpr(expr->expr, lc);
        return true;
    }
    if (auto comp = std::dynamic_pointer_cast<CompoundStatement>(stmt)) {
        for (auto &s : comp->statements) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) break;
        }
        return true;
    }
    lc.diags.Report(stmt->loc, "Unsupported statement in lowering");
    return false;
}

bool LowerFunction(const FunctionDecl &fn, LoweringContext &lc) {
    // Map signature (minimal: primitive ints/bools/void)
    ir::IRType ret_ty = ToIRType(fn.return_type);
    if (ret_ty.kind == ir::IRTypeKind::kInvalid) ret_ty = ir::IRType::I64(true);

    std::vector<std::pair<std::string, ir::IRType>> params;
    params.reserve(fn.params.size());
    for (auto &p : fn.params) {
        ir::IRType pt = ToIRType(p.type);
        if (pt.kind == ir::IRTypeKind::kInvalid) {
            lc.diags.Report(p.type ? p.type->loc : fn.loc, "Unsupported parameter type");
            return false;
        }
        params.push_back({p.name, pt});
    }

    lc.fn = lc.ir_ctx.CreateFunction(fn.name, ret_ty, params);
    // Create entry block and start inserting there.
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    if (!lc.fn->blocks.empty()) {
        lc.builder.SetInsertPoint(lc.fn->blocks.back());
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

}  // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
    LoweringContext lc(ctx, diags);
    for (const auto &decl : module.declarations) {
        auto fn = std::dynamic_pointer_cast<FunctionDecl>(decl);
        if (!fn) continue;
        if (fn->is_deleted || fn->is_defaulted) continue;
        LowerFunction(*fn, lc);
    }
}

}  // namespace polyglot::cpp
