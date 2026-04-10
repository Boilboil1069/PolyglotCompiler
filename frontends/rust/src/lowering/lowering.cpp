/**
 * @file     lowering.cpp
 * @brief    Rust language frontend implementation
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/rust/include/rust_lowering.h"
#include "frontends/rust/include/rust_ast.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "middle/include/ir/ir_builder.h"
#include "middle/include/ir/ir_printer.h"

namespace polyglot::rust {
namespace {

using Name = std::string;

// Convert Rust type to IR type - handles all common Rust types
ir::IRType ToIRType(const std::shared_ptr<TypeNode> &type) {
    if (!type) return ir::IRType::I64(true);
    
    if (auto path = std::dynamic_pointer_cast<TypePath>(type)) {
        std::string type_name = path->segments.empty() ? "" : path->segments.back();
        
        // Integer types
        if (type_name == "i8") return ir::IRType::I8(true);
        if (type_name == "i16") return ir::IRType::I16(true);
        if (type_name == "i32") return ir::IRType::I32(true);
        if (type_name == "i64") return ir::IRType::I64(true);
        if (type_name == "i128") return ir::IRType::I64(true);  // Treat as i64 for now
        if (type_name == "isize") return ir::IRType::I64(true);  // Platform-dependent
        
        // Unsigned integer types
        if (type_name == "u8") return ir::IRType::I8(false);
        if (type_name == "u16") return ir::IRType::I16(false);
        if (type_name == "u32") return ir::IRType::I32(false);
        if (type_name == "u64") return ir::IRType::I64(false);
        if (type_name == "u128") return ir::IRType::I64(false);  // Treat as u64 for now
        if (type_name == "usize") return ir::IRType::I64(false);  // Platform-dependent
        
        // Floating point types
        if (type_name == "f32") return ir::IRType::F32();
        if (type_name == "f64") return ir::IRType::F64();
        
        // Boolean type
        if (type_name == "bool") return ir::IRType::I1();
        
        // Character type (32-bit Unicode scalar value)
        if (type_name == "char") return ir::IRType::I32(false);
        
        // Unit type (void)
        if (type_name == "()" || path->segments.empty()) return ir::IRType::Void();
        
        // String type - represented as pointer to i8
        if (type_name == "str" || type_name == "String") {
            return ir::IRType::Pointer(ir::IRType::I8());
        }
        
        // Default to i64 for unknown Rust types (warn at runtime)
        std::cerr << "[rust-lowering] unknown type '" << type_name
                  << "'; defaulting to i64\n";
        return ir::IRType::I64(true);
    }
    
    // Reference types become pointers
    if (auto ref = std::dynamic_pointer_cast<ReferenceType>(type)) {
        auto inner = ToIRType(ref->inner);
        return ir::IRType::Pointer(inner);
    }
    
    // Slice types become pointers (fat pointers in reality)
    if (auto slice = std::dynamic_pointer_cast<SliceType>(type)) {
        auto inner = ToIRType(slice->inner);
        return ir::IRType::Pointer(inner);
    }
    
    // Array types become pointers
    if (auto arr = std::dynamic_pointer_cast<ArrayType>(type)) {
        auto inner = ToIRType(arr->inner);
        return ir::IRType::Pointer(inner);
    }
    
    // Tuple types - use struct representation
    if (auto tup = std::dynamic_pointer_cast<TupleType>(type)) {
        if (tup->elements.empty()) return ir::IRType::Void();
        // For now, tuples with single element return that element type
        if (tup->elements.size() == 1) return ToIRType(tup->elements[0]);
        // Complex tuples as struct
        std::vector<ir::IRType> fields;
        for (const auto &elem : tup->elements) {
            fields.push_back(ToIRType(elem));
        }
        return ir::IRType::Struct("tuple", fields);
    }
    
    // Function types become function pointers
    if (auto fn = std::dynamic_pointer_cast<FunctionType>(type)) {
        auto ret = ToIRType(fn->return_type);
        std::vector<ir::IRType> params;
        for (const auto &p : fn->params) {
            params.push_back(ToIRType(p));
        }
        return ir::IRType::Pointer(ir::IRType::Function(ret, params));
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
    // Break/continue target blocks for loops
    ir::BasicBlock *loop_exit{nullptr};
    ir::BasicBlock *loop_continue{nullptr};

    LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d)
        : ir_ctx(ctx), diags(d), builder(ctx) {}
};

struct EvalResult {
    Name value;
    ir::IRType type{ir::IRType::Invalid()};
};

// Check if a string is a valid integer literal
bool IsIntegerLiteral(const std::string &text, long long *out) {
    if (text.empty()) return false;
    char *end = nullptr;
    long long v = std::strtoll(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0') return false;
    if (out) *out = v;
    return true;
}

// Check if a string is a valid floating point literal
bool IsFloatLiteral(const std::string &text, double *out) {
    if (text.empty()) return false;
    char *end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || (*end != '\0' && *end != 'f')) return false;
    if (out) *out = v;
    return true;
}

// Forward declarations
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc);
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);
bool LowerFunction(const FunctionItem &fn, LoweringContext &lc);

// Create an integer literal
EvalResult MakeLiteral(long long v, LoweringContext &lc) {
    (void)lc;
    return {std::to_string(v), ir::IRType::I64(true)};
}

// Create a floating point literal
EvalResult MakeFloatLiteral(double v, LoweringContext &lc) {
    (void)lc;
    // Format with enough precision
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return {buf, ir::IRType::F64()};
}

// Create a boolean literal
EvalResult MakeBoolLiteral(bool v, LoweringContext &lc) {
    (void)lc;
    return {v ? "1" : "0", ir::IRType::I1()};
}

// Evaluate path expression (variable reference)
EvalResult EvalPath(const std::shared_ptr<PathExpression> &path, LoweringContext &lc) {
    std::string path_name = path->segments.empty() ? "" : path->segments.back();
    auto it = lc.env.find(path_name);
    if (it == lc.env.end()) {
        lc.diags.Report(path->loc, "undefined path: " + path_name);
        return {};
    }
    return {it->second.value, it->second.type};
}

// Map Rust binary operator to IR operator
ir::BinaryInstruction::Op MapBinOp(const std::string &op, bool is_signed = true, bool is_float = false) {
    // Arithmetic operators
    if (op == "+") return ir::BinaryInstruction::Op::kAdd;
    if (op == "-") return ir::BinaryInstruction::Op::kSub;
    if (op == "*") return ir::BinaryInstruction::Op::kMul;
    if (op == "/") {
        if (is_float) return ir::BinaryInstruction::Op::kFDiv;
        return is_signed ? ir::BinaryInstruction::Op::kSDiv : ir::BinaryInstruction::Op::kUDiv;
    }
    if (op == "%") {
        return is_signed ? ir::BinaryInstruction::Op::kSRem : ir::BinaryInstruction::Op::kURem;
    }
    
    // Comparison operators
    if (op == "==") return ir::BinaryInstruction::Op::kCmpEq;
    if (op == "!=") return ir::BinaryInstruction::Op::kCmpNe;
    if (op == "<") return is_signed ? ir::BinaryInstruction::Op::kCmpSlt : ir::BinaryInstruction::Op::kCmpUlt;
    if (op == "<=") return is_signed ? ir::BinaryInstruction::Op::kCmpSle : ir::BinaryInstruction::Op::kCmpUle;
    if (op == ">") return is_signed ? ir::BinaryInstruction::Op::kCmpSgt : ir::BinaryInstruction::Op::kCmpUgt;
    if (op == ">=") return is_signed ? ir::BinaryInstruction::Op::kCmpSge : ir::BinaryInstruction::Op::kCmpUge;
    
    // Bitwise operators
    if (op == "&") return ir::BinaryInstruction::Op::kAnd;
    if (op == "|") return ir::BinaryInstruction::Op::kOr;
    if (op == "^") return ir::BinaryInstruction::Op::kXor;
    if (op == "<<") return ir::BinaryInstruction::Op::kShl;
    if (op == ">>") return is_signed ? ir::BinaryInstruction::Op::kAShr : ir::BinaryInstruction::Op::kLShr;
    
    return ir::BinaryInstruction::Op::kAdd;
}

// Check if operator is a comparison
bool IsComparisonOp(const std::string &op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

// Evaluate binary expression
EvalResult EvalBinary(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
    // Handle short-circuit logical operators
    if (bin->op == "&&") {
        auto lhs = EvalExpr(bin->left, lc);
        if (lhs.type.kind == ir::IRTypeKind::kInvalid) return {};
        
        auto *current_block = lc.builder.GetInsertPoint().get();
        auto *rhs_block = lc.fn->CreateBlock("and.rhs");
        auto *merge_block = lc.fn->CreateBlock("and.merge");
        
        lc.builder.MakeCondBranch(lhs.value, rhs_block, merge_block);
        
        // Evaluate RHS
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == rhs_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        auto rhs = EvalExpr(bin->right, lc);
        auto *rhs_end_block = lc.builder.GetInsertPoint().get();
        lc.builder.MakeBranch(merge_block);
        
        // Merge block with PHI
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == merge_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        std::vector<std::pair<ir::BasicBlock*, std::string>> incomings;
        incomings.push_back({current_block, "0"});  // false from short-circuit
        incomings.push_back({rhs_end_block, rhs.value});  // rhs.value from rhs_block
        auto phi = lc.builder.MakePhi(ir::IRType::I1(), incomings, "and.result");
        return {phi->name, ir::IRType::I1()};
    }
    
    if (bin->op == "||") {
        auto lhs = EvalExpr(bin->left, lc);
        if (lhs.type.kind == ir::IRTypeKind::kInvalid) return {};
        
        auto *current_block = lc.builder.GetInsertPoint().get();
        auto *rhs_block = lc.fn->CreateBlock("or.rhs");
        auto *merge_block = lc.fn->CreateBlock("or.merge");
        
        lc.builder.MakeCondBranch(lhs.value, merge_block, rhs_block);
        
        // Evaluate RHS
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == rhs_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        auto rhs = EvalExpr(bin->right, lc);
        auto *rhs_end_block = lc.builder.GetInsertPoint().get();
        lc.builder.MakeBranch(merge_block);
        
        // Merge block with PHI
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == merge_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        std::vector<std::pair<ir::BasicBlock*, std::string>> incomings;
        incomings.push_back({current_block, "1"});  // true from short-circuit
        incomings.push_back({rhs_end_block, rhs.value});  // rhs.value from rhs_block
        auto phi = lc.builder.MakePhi(ir::IRType::I1(), incomings, "or.result");
        return {phi->name, ir::IRType::I1()};
    }
    
    auto lhs = EvalExpr(bin->left, lc);
    auto rhs = EvalExpr(bin->right, lc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid || rhs.type.kind == ir::IRTypeKind::kInvalid)
        return {};
    
    bool is_signed = lhs.type.is_signed;
    bool is_float = lhs.type.kind == ir::IRTypeKind::kF32 || lhs.type.kind == ir::IRTypeKind::kF64;
    ir::BinaryInstruction::Op op = MapBinOp(bin->op, is_signed, is_float);
    auto inst = lc.builder.MakeBinary(op, lhs.value, rhs.value, "");
    
    // Set result type based on operation
    if (IsComparisonOp(bin->op)) {
        inst->type = ir::IRType::I1();
    } else {
        inst->type = lhs.type;
    }
    return {inst->name, inst->type};
}

// Evaluate unary expression
EvalResult EvalUnary(const std::shared_ptr<UnaryExpression> &un, LoweringContext &lc) {
    auto operand = EvalExpr(un->operand, lc);
    if (operand.type.kind == ir::IRTypeKind::kInvalid) return {};
    
    if (un->op == "-") {
        // Negation: 0 - operand
        auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kSub, "0", operand.value, "");
        inst->type = operand.type;
        return {inst->name, inst->type};
    }
    
    if (un->op == "!") {
        // Logical not: xor with 1
        auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor, operand.value, "1", "");
        inst->type = ir::IRType::I1();
        return {inst->name, inst->type};
    }
    
    if (un->op == "&" || un->op == "&mut") {
        // Reference: return pointer to operand
        // In simple IR, we treat this as identity for now
        return {operand.value, ir::IRType::Pointer(operand.type)};
    }
    
    if (un->op == "*") {
        // Dereference: load from pointer
        ir::IRType inner_type = ir::IRType::I64(true);
        if (operand.type.kind == ir::IRTypeKind::kPointer && !operand.type.subtypes.empty()) {
            inner_type = operand.type.subtypes[0];
        }
        auto inst = lc.builder.MakeLoad(operand.value, inner_type, "");
        return {inst->name, inst->type};
    }
    
    return operand;
}

// Evaluate function call expression
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
    } else if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
        callee_name = id->name;
    } else {
        lc.diags.Report(call->loc, "only direct function calls are supported");
        return {};
    }
    
    auto inst = lc.builder.MakeCall(callee_name, args, ir::IRType::I64(true), "");
    return {inst->name, inst->type};
}

// Evaluate identifier expression
EvalResult EvalIdentifier(const std::shared_ptr<Identifier> &id, LoweringContext &lc) {
    auto it = lc.env.find(id->name);
    if (it == lc.env.end()) {
        lc.diags.Report(id->loc, "undefined identifier: " + id->name);
        return {};
    }
    return {it->second.value, it->second.type};
}

// Evaluate literal expression
EvalResult EvalLiteral(const std::shared_ptr<Literal> &lit, LoweringContext &lc) {
    // Boolean literals
    if (lit->value == "true") return MakeBoolLiteral(true, lc);
    if (lit->value == "false") return MakeBoolLiteral(false, lc);
    
    // Check for float literal (contains '.' or 'f' suffix)
    if (lit->value.find('.') != std::string::npos || 
        (!lit->value.empty() && lit->value.back() == 'f')) {
        double v{};
        if (IsFloatLiteral(lit->value, &v)) {
            return MakeFloatLiteral(v, lc);
        }
    }
    
    // Integer literal
    long long v{};
    if (IsIntegerLiteral(lit->value, &v)) {
        return MakeLiteral(v, lc);
    }
    
    // String literal - return as constant string pointer
    auto str_ptr = lc.builder.MakeStringLiteral(lit->value, "str");
    return {str_ptr, ir::IRType::Pointer(ir::IRType::I8())};
}

// Evaluate member expression (struct field access)
EvalResult EvalMember(const std::shared_ptr<MemberExpression> &mem, LoweringContext &lc) {
    auto obj = EvalExpr(mem->object, lc);
    if (obj.type.kind == ir::IRTypeKind::kInvalid) return {};
    
    // For now, emit a GEP-like instruction for field access
    std::string field_name = obj.value + "." + mem->member;
    return {field_name, ir::IRType::I64(true)};  // Type would need lookup
}

// Evaluate index expression (array/slice indexing)
EvalResult EvalIndex(const std::shared_ptr<IndexExpression> &idx, LoweringContext &lc) {
    auto obj = EvalExpr(idx->object, lc);
    auto index = EvalExpr(idx->index, lc);
    if (obj.type.kind == ir::IRTypeKind::kInvalid || index.type.kind == ir::IRTypeKind::kInvalid)
        return {};
    
    // Determine element type
    ir::IRType elem_type = ir::IRType::I64(true);
    if (obj.type.kind == ir::IRTypeKind::kPointer && !obj.type.subtypes.empty()) {
        elem_type = obj.type.subtypes[0];
    } else if (obj.type.kind == ir::IRTypeKind::kArray && !obj.type.subtypes.empty()) {
        elem_type = obj.type.subtypes[0];
    }
    
    // Generate dynamic array access using arithmetic
    auto gep = lc.builder.MakeDynamicGEP(obj.value, elem_type, index.value, "");
    auto load = lc.builder.MakeLoad(gep->name, elem_type, "");
    return {load->name, load->type};
}

// Evaluate assignment expression
EvalResult EvalAssignment(const std::shared_ptr<AssignmentExpression> &assign, LoweringContext &lc) {
    auto rhs = EvalExpr(assign->right, lc);
    if (rhs.type.kind == ir::IRTypeKind::kInvalid) return {};
    
    // Handle compound assignments (+=, -=, etc.)
    if (assign->op != "=") {
        auto lhs = EvalExpr(assign->left, lc);
        if (lhs.type.kind == ir::IRTypeKind::kInvalid) return {};
        
        std::string base_op = assign->op.substr(0, assign->op.size() - 1);
        auto op = MapBinOp(base_op, lhs.type.is_signed);
        auto inst = lc.builder.MakeBinary(op, lhs.value, rhs.value, "");
        inst->type = lhs.type;
        rhs = {inst->name, inst->type};
    }
    
    // Store the result
    if (auto id = std::dynamic_pointer_cast<Identifier>(assign->left)) {
        lc.env[id->name] = {rhs.value, rhs.type};
        return rhs;
    }
    if (auto path = std::dynamic_pointer_cast<PathExpression>(assign->left)) {
        std::string name = path->segments.empty() ? "" : path->segments.back();
        lc.env[name] = {rhs.value, rhs.type};
        return rhs;
    }
    
    lc.diags.Report(assign->loc, "unsupported assignment target");
    return {};
}

// Evaluate if expression (returns value)
EvalResult EvalIfExpr(const std::shared_ptr<IfExpression> &if_expr, LoweringContext &lc) {
    auto cond = EvalExpr(if_expr->condition, lc);
    if (cond.type.kind == ir::IRTypeKind::kInvalid) return {};
    
    auto *then_block = lc.fn->CreateBlock("if.then");
    auto *else_block = if_expr->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
    auto *merge_block = lc.fn->CreateBlock("if.end");
    
    lc.builder.MakeCondBranch(cond.value, then_block,
                              else_block ? else_block : merge_block);
    
    // Then block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == then_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    EvalResult then_result{};
    for (auto &s : if_expr->then_body) {
        if (!LowerStmt(s, lc)) return {};
        if (lc.terminated) break;
    }
    if (!lc.terminated) {
        lc.builder.MakeBranch(merge_block);
    }
    
    // Else block
    EvalResult else_result{};
    if (else_block) {
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == else_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        lc.terminated = false;
        for (auto &s : if_expr->else_body) {
            if (!LowerStmt(s, lc)) return {};
            if (lc.terminated) break;
        }
        if (!lc.terminated) {
            lc.builder.MakeBranch(merge_block);
        }
    }
    
    // Merge block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == merge_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    
    // If used as expression, would need PHI node here
    return {merge_block->name, ir::IRType::Void()};
}

// Evaluate block expression
EvalResult EvalBlock(const std::shared_ptr<BlockExpression> &blk, LoweringContext &lc) {
    EvalResult last{};
    for (auto &s : blk->statements) {
        if (!LowerStmt(s, lc)) return {};
        if (lc.terminated) break;
    }
    // Block expression evaluates to the last expression (if any)
    return last;
}

// Evaluate match expression
EvalResult EvalMatch(const std::shared_ptr<MatchExpression> &match, LoweringContext &lc) {
    auto scrutinee = EvalExpr(match->scrutinee, lc);
    if (scrutinee.type.kind == ir::IRTypeKind::kInvalid) return {};
    
    auto *merge_block = lc.fn->CreateBlock("match.end");
    
    // For each arm, create a block and comparison
    for (size_t i = 0; i < match->arms.size(); ++i) {
        const auto &arm = match->arms[i];
        auto *arm_block = lc.fn->CreateBlock("match.arm." + std::to_string(i));
        auto *next_block = (i + 1 < match->arms.size()) 
                           ? lc.fn->CreateBlock("match.next." + std::to_string(i))
                           : merge_block;
        
        // Pattern matching - simplified to literal/wildcard comparison
        bool is_wildcard = false;
        if (auto wild = std::dynamic_pointer_cast<WildcardPattern>(arm->pattern)) {
            is_wildcard = true;
        } else if (auto id = std::dynamic_pointer_cast<IdentifierPattern>(arm->pattern)) {
            is_wildcard = (id->name == "_");
        }
        
        if (is_wildcard) {
            // Wildcard matches everything - unconditional branch
            lc.builder.MakeBranch(arm_block);
        } else if (auto lit = std::dynamic_pointer_cast<LiteralPattern>(arm->pattern)) {
            // Compare against literal
            auto cmp = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpEq,
                                             scrutinee.value, lit->value, "");
            cmp->type = ir::IRType::I1();
            lc.builder.MakeCondBranch(cmp->name, arm_block, next_block);
        } else {
            // Default to unconditional for complex patterns
            lc.builder.MakeBranch(arm_block);
        }
        
        // Arm body
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == arm_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        lc.terminated = false;
        EvalExpr(arm->body, lc);
        if (!lc.terminated) {
            lc.builder.MakeBranch(merge_block);
        }
        
        // Next block for next arm comparison
        if (next_block != merge_block) {
            for (auto &bb : lc.fn->blocks) {
                if (bb.get() == next_block) {
                    lc.builder.SetInsertPoint(bb);
                    break;
                }
            }
        }
    }
    
    // Merge block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == merge_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    return {merge_block->name, ir::IRType::Void()};
}

// Evaluate range expression
EvalResult EvalRange(const std::shared_ptr<RangeExpression> &range, LoweringContext &lc) {
    EvalResult start{}, end{};
    if (range->start) start = EvalExpr(range->start, lc);
    if (range->end) end = EvalExpr(range->end, lc);
    
    // Range is represented as a pair (start, end)
    // For iteration, this would be expanded in the for loop lowering
    return start;  // Return start for now
}

// Evaluate closure expression
EvalResult EvalClosure(const std::shared_ptr<ClosureExpression> &cls, LoweringContext &lc) {
    // Create an anonymous function for the closure
    std::string closure_name = "__closure_" + std::to_string(reinterpret_cast<uintptr_t>(cls.get()));
    
    // For now, just return a function pointer
    return {closure_name, ir::IRType::Pointer(ir::IRType::Function(ir::IRType::I64(true), {}))};
}

// Main expression evaluation dispatcher
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
    if (!expr) return {};
    
    // Literal
    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        return EvalLiteral(lit, lc);
    }
    
    // Identifier
    if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
        return EvalIdentifier(id, lc);
    }
    
    // Path expression (variable or type path)
    if (auto path = std::dynamic_pointer_cast<PathExpression>(expr)) {
        return EvalPath(path, lc);
    }
    
    // Binary expression
    if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return EvalBinary(bin, lc);
    }
    
    // Unary expression
    if (auto un = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
        return EvalUnary(un, lc);
    }
    
    // Call expression
    if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
        return EvalCall(call, lc);
    }
    
    // Member expression (field access)
    if (auto mem = std::dynamic_pointer_cast<MemberExpression>(expr)) {
        return EvalMember(mem, lc);
    }
    
    // Index expression (array access)
    if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
        return EvalIndex(idx, lc);
    }
    
    // Assignment expression
    if (auto assign = std::dynamic_pointer_cast<AssignmentExpression>(expr)) {
        return EvalAssignment(assign, lc);
    }
    
    // If expression
    if (auto if_expr = std::dynamic_pointer_cast<IfExpression>(expr)) {
        return EvalIfExpr(if_expr, lc);
    }
    
    // Block expression
    if (auto blk = std::dynamic_pointer_cast<BlockExpression>(expr)) {
        return EvalBlock(blk, lc);
    }
    
    // Match expression
    if (auto match = std::dynamic_pointer_cast<MatchExpression>(expr)) {
        return EvalMatch(match, lc);
    }
    
    // Range expression
    if (auto range = std::dynamic_pointer_cast<RangeExpression>(expr)) {
        return EvalRange(range, lc);
    }
    
    // Closure expression
    if (auto cls = std::dynamic_pointer_cast<ClosureExpression>(expr)) {
        return EvalClosure(cls, lc);
    }
    
    // While expression (as statement-like expression)
    if (auto wh = std::dynamic_pointer_cast<WhileExpression>(expr)) {
        // Lower as loop
        auto *cond_block = lc.fn->CreateBlock("while.cond");
        auto *body_block = lc.fn->CreateBlock("while.body");
        auto *exit_block = lc.fn->CreateBlock("while.exit");
        
        lc.builder.MakeBranch(cond_block);
        
        // Condition block
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == cond_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        auto cond = EvalExpr(wh->condition, lc);
        lc.builder.MakeCondBranch(cond.value, body_block, exit_block);
        
        // Body block
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == body_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        
        // Save and set loop targets
        auto *old_exit = lc.loop_exit;
        auto *old_continue = lc.loop_continue;
        lc.loop_exit = exit_block;
        lc.loop_continue = cond_block;
        
        lc.terminated = false;
        for (auto &s : wh->body) {
            if (!LowerStmt(s, lc)) return {};
            if (lc.terminated) break;
        }
        if (!lc.terminated) {
            lc.builder.MakeBranch(cond_block);
        }
        
        lc.loop_exit = old_exit;
        lc.loop_continue = old_continue;
        
        // Exit block
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == exit_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        lc.terminated = false;
        return {exit_block->name, ir::IRType::Void()};
    }
    
    // Macro call — expand well-known macros into IR.
    // For user-defined macros we emit a call to a runtime helper that
    // evaluates the macro body; for std macros like println!/format! we
    // expand them inline.
    if (auto macro = std::dynamic_pointer_cast<MacroCallExpression>(expr)) {
        // Construct the full macro path (e.g. "std::println", "vec")
        std::string macro_name;
        for (const auto &seg : macro->path.segments) {
            if (!macro_name.empty()) macro_name += "::";
            macro_name += seg;
        }

        if (macro_name == "println" || macro_name == "std::println") {
            // println!("...") → call __rs_println with the string body
            std::string body_str = macro->body;
            std::string sym = lc.builder.MakeStringLiteral(body_str, "fmt");
            auto inst = lc.builder.MakeCall("__rs_println", {sym}, ir::IRType::Void(), "");
            return {inst->name, ir::IRType::Void()};
        }
        if (macro_name == "eprintln" || macro_name == "std::eprintln") {
            std::string body_str = macro->body;
            std::string sym = lc.builder.MakeStringLiteral(body_str, "fmt");
            auto inst = lc.builder.MakeCall("__rs_eprintln", {sym}, ir::IRType::Void(), "");
            return {inst->name, ir::IRType::Void()};
        }
        if (macro_name == "format" || macro_name == "std::format") {
            std::string body_str = macro->body;
            std::string sym = lc.builder.MakeStringLiteral(body_str, "fmt");
            auto inst = lc.builder.MakeCall("__rs_format", {sym},
                                            ir::IRType::Pointer(ir::IRType::I8()), "");
            return {inst->name, ir::IRType::Pointer(ir::IRType::I8())};
        }
        if (macro_name == "vec" || macro_name == "std::vec") {
            // vec![...] → allocate a runtime vector from the body tokens
            std::string body_str = macro->body;
            std::string sym = lc.builder.MakeStringLiteral(body_str, "vec_init");
            auto inst = lc.builder.MakeCall("__rs_vec_from_literal", {sym},
                                            ir::IRType::Pointer(ir::IRType::I8()), "");
            return {inst->name, ir::IRType::Pointer(ir::IRType::I8())};
        }
        if (macro_name == "panic" || macro_name == "std::panic") {
            std::string body_str = macro->body;
            std::string sym = lc.builder.MakeStringLiteral(body_str, "panic_msg");
            lc.builder.MakeCall("__rs_panic", {sym}, ir::IRType::Void(), "");
            lc.builder.MakeUnreachable();
            lc.terminated = true;
            return {"", ir::IRType::Void()};
        }

        // Generic macro: emit a runtime call with the macro body as a string
        std::string sym = lc.builder.MakeStringLiteral(macro->body, "macro_body");
        std::string func = "__rs_macro_" + macro_name;
        for (char &c : func) {
            if (c == ':') c = '_';
        }
        auto inst = lc.builder.MakeCall(func, {sym}, ir::IRType::I64(true), "");
        return {inst->name, ir::IRType::I64(true)};
    }
    
    // Try expression
    if (auto try_expr = std::dynamic_pointer_cast<TryExpression>(expr)) {
        return EvalExpr(try_expr->value, lc);
    }
    
    // Await expression
    if (auto await = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
        return EvalExpr(await->future ? await->future : await->value, lc);
    }
    
    lc.diags.Report(expr->loc, "unsupported expression type in lowering");
    return {};
}

// Lower return statement
bool LowerReturn(const std::shared_ptr<ReturnStatement> &ret, LoweringContext &lc) {
    if (lc.terminated) return true;
    EvalResult v;
    if (ret->value) v = EvalExpr(ret->value, lc);
    lc.builder.MakeReturn(v.value);
    lc.terminated = true;
    return true;
}

// Lower let binding statement
bool LowerLet(const std::shared_ptr<LetStatement> &let, LoweringContext &lc) {
    EvalResult result;
    if (let->init) {
        result = EvalExpr(let->init, lc);
        if (result.type.kind == ir::IRTypeKind::kInvalid) {
            lc.diags.Report(let->loc, "failed to evaluate let initializer");
            return false;
        }
    } else {
        // Uninitialized binding - use zero/default
        ir::IRType ty = ToIRType(let->type_annotation);
        result = {"0", ty};
    }
    
    // Handle different pattern types
    if (auto pat = std::dynamic_pointer_cast<IdentifierPattern>(let->pattern)) {
        lc.env[pat->name] = {result.value, result.type};
        return true;
    }
    
    if (auto tup = std::dynamic_pointer_cast<TuplePattern>(let->pattern)) {
        // Destructuring tuple pattern: let (a, b, c) = expr;
        // Extract each element from the tuple value using GEP.
        if (result.type.kind == ir::IRTypeKind::kStruct &&
            !result.type.subtypes.empty()) {
            // The result is a struct type representing the tuple.
            for (size_t i = 0; i < tup->elements.size() && i < result.type.subtypes.size(); ++i) {
                auto elem_pat = std::dynamic_pointer_cast<IdentifierPattern>(tup->elements[i]);
                if (elem_pat) {
                    auto gep = lc.builder.MakeGEP(result.value, result.type, {i});
                    auto load = lc.builder.MakeLoad(gep->name, result.type.subtypes[i]);
                    lc.env[elem_pat->name] = {load->name, result.type.subtypes[i]};
                }
                // Wildcard patterns are silently ignored.
            }
        } else {
            // Non-struct tuple value: extract elements using offset arithmetic.
            for (size_t i = 0; i < tup->elements.size(); ++i) {
                auto elem_pat = std::dynamic_pointer_cast<IdentifierPattern>(tup->elements[i]);
                if (elem_pat) {
                    // Use a runtime helper to extract element i from the tuple
                    std::string idx_str = std::to_string(i);
                    auto extract = lc.builder.MakeCall(
                        "__rs_tuple_extract",
                        {result.value, idx_str},
                        ir::IRType::I64(true), "");
                    lc.env[elem_pat->name] = {extract->name, ir::IRType::I64(true)};
                }
            }
        }
        return true;
    }
    
    if (auto wild = std::dynamic_pointer_cast<WildcardPattern>(let->pattern)) {
        // Wildcard pattern - just evaluate the expression for side effects
        return true;
    }
    
    lc.diags.Report(let->loc, "unsupported pattern type in let binding");
    return false;
}

// Lower if statement/expression
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

// Lower loop statement (infinite loop)
bool LowerLoop(const std::shared_ptr<LoopStatement> &loop, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    auto *body_block = lc.fn->CreateBlock("loop.body");
    auto *exit_block = lc.fn->CreateBlock("loop.exit");
    
    // Save and set loop targets for break/continue
    auto *old_exit = lc.loop_exit;
    auto *old_continue = lc.loop_continue;
    lc.loop_exit = exit_block;
    lc.loop_continue = body_block;
    
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
    
    lc.loop_exit = old_exit;
    lc.loop_continue = old_continue;
    
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == exit_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    return true;
}

// Lower while expression as statement
bool LowerWhile(const std::shared_ptr<WhileExpression> &wh, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    auto *cond_block = lc.fn->CreateBlock("while.cond");
    auto *body_block = lc.fn->CreateBlock("while.body");
    auto *exit_block = lc.fn->CreateBlock("while.exit");
    
    // Save and set loop targets
    auto *old_exit = lc.loop_exit;
    auto *old_continue = lc.loop_continue;
    lc.loop_exit = exit_block;
    lc.loop_continue = cond_block;
    
    lc.builder.MakeBranch(cond_block);
    
    // Condition block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == cond_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    auto cond = EvalExpr(wh->condition, lc);
    lc.builder.MakeCondBranch(cond.value, body_block, exit_block);
    
    // Body block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == body_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    
    for (auto &s : wh->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }
    
    if (!lc.terminated) {
        lc.builder.MakeBranch(cond_block);
    }
    
    lc.loop_exit = old_exit;
    lc.loop_continue = old_continue;
    
    // Exit block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == exit_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    return true;
}

// Lower for statement
bool LowerFor(const std::shared_ptr<ForStatement> &fr, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    // Evaluate the iterable
    auto iterable = EvalExpr(fr->iterable, lc);
    
    // For range-based iteration, we need to handle Range expressions
    // For now, create a simple loop structure
    auto *cond_block = lc.fn->CreateBlock("for.cond");
    auto *body_block = lc.fn->CreateBlock("for.body");
    auto *incr_block = lc.fn->CreateBlock("for.incr");
    auto *exit_block = lc.fn->CreateBlock("for.exit");
    
    // Save and set loop targets
    auto *old_exit = lc.loop_exit;
    auto *old_continue = lc.loop_continue;
    lc.loop_exit = exit_block;
    lc.loop_continue = incr_block;
    
    // Initialize loop variable from pattern
    std::string loop_var;
    if (auto id = std::dynamic_pointer_cast<IdentifierPattern>(fr->pattern)) {
        loop_var = id->name;
        lc.env[loop_var] = {iterable.value, iterable.type};
    }
    
    lc.builder.MakeBranch(cond_block);
    
    // Condition block (for range, check if counter < end)
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == cond_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    // Simple condition: always true for now (infinite iteration guard would be needed)
    lc.builder.MakeCondBranch("1", body_block, exit_block);
    
    // Body block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == body_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    
    for (auto &s : fr->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }
    
    if (!lc.terminated) {
        lc.builder.MakeBranch(incr_block);
    }
    
    // Increment block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == incr_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    // Increment loop variable
    if (!loop_var.empty()) {
        auto &entry = lc.env[loop_var];
        auto inc = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kAdd, entry.value, "1", "");
        inc->type = entry.type;
        entry.value = inc->name;
    }
    lc.builder.MakeBranch(cond_block);
    
    lc.loop_exit = old_exit;
    lc.loop_continue = old_continue;
    
    // Exit block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == exit_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    return true;
}

// Lower break statement
bool LowerBreak(const std::shared_ptr<BreakStatement> &brk, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    if (!lc.loop_exit) {
        lc.diags.Report(brk->loc, "break outside of loop");
        return false;
    }
    
    // Evaluate break value if present (for loop expressions)
    if (brk->value) {
        EvalExpr(brk->value, lc);
    }
    
    lc.builder.MakeBranch(lc.loop_exit);
    lc.terminated = true;
    return true;
}

// Lower continue statement
bool LowerContinue(const std::shared_ptr<ContinueStatement> &cont, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    if (!lc.loop_continue) {
        lc.diags.Report(cont->loc, "continue outside of loop");
        return false;
    }
    
    lc.builder.MakeBranch(lc.loop_continue);
    lc.terminated = true;
    return true;
}

// Lower block expression as statement
bool LowerBlock(const std::shared_ptr<BlockExpression> &blk, LoweringContext &lc) {
    for (auto &s : blk->statements) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }
    return true;
}

// Main statement lowering dispatcher
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
    if (!stmt || lc.terminated) return true;
    
    // Return statement
    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
        return LowerReturn(ret, lc);
    }
    
    // Let statement
    if (auto let = std::dynamic_pointer_cast<LetStatement>(stmt)) {
        return LowerLet(let, lc);
    }
    
    // Loop statement
    if (auto loop = std::dynamic_pointer_cast<LoopStatement>(stmt)) {
        return LowerLoop(loop, lc);
    }
    
    // For statement
    if (auto fr = std::dynamic_pointer_cast<ForStatement>(stmt)) {
        return LowerFor(fr, lc);
    }
    
    // Break statement
    if (auto brk = std::dynamic_pointer_cast<BreakStatement>(stmt)) {
        return LowerBreak(brk, lc);
    }
    
    // Continue statement
    if (auto cont = std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
        return LowerContinue(cont, lc);
    }
    
    // Expression statement
    if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        // Check for specific expression types that need special handling
        if (auto if_expr = std::dynamic_pointer_cast<IfExpression>(expr_stmt->expr)) {
            return LowerIf(if_expr, lc);
        }
        if (auto wh = std::dynamic_pointer_cast<WhileExpression>(expr_stmt->expr)) {
            return LowerWhile(wh, lc);
        }
        if (auto blk = std::dynamic_pointer_cast<BlockExpression>(expr_stmt->expr)) {
            return LowerBlock(blk, lc);
        }
        // General expression - evaluate for side effects
        (void)EvalExpr(expr_stmt->expr, lc);
        return true;
    }
    
    // If expression as statement
    if (auto if_expr = std::dynamic_pointer_cast<IfExpression>(stmt)) {
        return LowerIf(if_expr, lc);
    }
    
    // While expression as statement
    if (auto wh = std::dynamic_pointer_cast<WhileExpression>(stmt)) {
        return LowerWhile(wh, lc);
    }
    
    // Block expression as statement
    if (auto blk = std::dynamic_pointer_cast<BlockExpression>(stmt)) {
        return LowerBlock(blk, lc);
    }
    
    // Match expression as statement
    if (auto match = std::dynamic_pointer_cast<MatchExpression>(stmt)) {
        EvalMatch(match, lc);
        return true;
    }

    // Function item declaration — lower as a standalone function
    if (auto fn_item = std::dynamic_pointer_cast<FunctionItem>(stmt)) {
        return LowerFunction(*fn_item, lc);
    }

    // Impl block — lower each method inside it
    if (auto impl = std::dynamic_pointer_cast<ImplItem>(stmt)) {
        for (const auto &m : impl->items) {
            if (auto fn_item = std::dynamic_pointer_cast<FunctionItem>(m)) {
                if (!LowerFunction(*fn_item, lc)) return false;
            }
        }
        return true;
    }

    // Const item — evaluate the initialiser and bind the result
    if (auto cst = std::dynamic_pointer_cast<ConstItem>(stmt)) {
        if (cst->value) {
            auto val = EvalExpr(cst->value, lc);
            lc.env[cst->name] = {val.value, val.type};
        }
        return true;
    }

    // Type alias, trait, mod, struct, enum, macro_rules — metadata only,
    // no IR is generated directly for these declarations.
    if (std::dynamic_pointer_cast<TypeAliasItem>(stmt) ||
        std::dynamic_pointer_cast<TraitItem>(stmt) ||
        std::dynamic_pointer_cast<ModItem>(stmt) ||
        std::dynamic_pointer_cast<StructItem>(stmt) ||
        std::dynamic_pointer_cast<EnumItem>(stmt) ||
        std::dynamic_pointer_cast<MacroRulesItem>(stmt)) {
        return true;
    }
    
    lc.diags.Report(stmt->loc, "unsupported statement type in lowering");
    return false;
}

// Lower a complete function
bool LowerFunction(const FunctionItem &fn, LoweringContext &lc) {
    ir::IRType ret_ty = ToIRType(fn.return_type);
    if (ret_ty.kind == ir::IRTypeKind::kInvalid) ret_ty = ir::IRType::Void();
    
    // Process parameters with full type support
    std::vector<std::pair<std::string, ir::IRType>> params;
    params.reserve(fn.params.size());
    for (auto &param : fn.params) {
        ir::IRType param_ty = ToIRType(param.type);
        if (param_ty.kind == ir::IRTypeKind::kInvalid) {
            // Unresolved parameter type — default to i64 and warn
            std::cerr << "[rust-lowering] unresolved type for parameter '"
                      << param.name << "' in function '" << fn.name
                      << "'; defaulting to i64\n";
            param_ty = ir::IRType::I64(true);
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
    
    // Initialize environment with parameters
    lc.env.clear();
    lc.loop_exit = nullptr;
    lc.loop_continue = nullptr;
    for (const auto &p : params) {
        lc.env[p.first] = {p.first, p.second};
    }
    lc.terminated = false;
    
    // Lower function body
    for (auto &stmt : fn.body) {
        if (!LowerStmt(stmt, lc)) return false;
        if (lc.terminated) break;
    }
    
    // Add implicit return if needed
    if (!lc.terminated) {
        if (ret_ty.kind == ir::IRTypeKind::kVoid) {
            lc.builder.MakeReturn("");
        } else {
            // Return zero/default for non-void functions
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
