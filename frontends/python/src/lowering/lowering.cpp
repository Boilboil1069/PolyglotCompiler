#include "frontends/python/include/python_lowering.h"
#include "frontends/python/include/python_ast.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/include/core/types.h"
#include "middle/include/ir/ir_builder.h"
#include "middle/include/ir/ir_printer.h"

namespace polyglot::python {
namespace {

using Name = std::string;

// ----------------------------------------------------------------------------
// Type mapping from Python type hints to IR types.
// ----------------------------------------------------------------------------
ir::IRType ToIRType(const std::string &type_hint) {
    if (type_hint == "int") return ir::IRType::I64(true);
    if (type_hint == "float") return ir::IRType::F64();
    if (type_hint == "bool") return ir::IRType::I1();
    if (type_hint == "None") return ir::IRType::Void();
    if (type_hint == "str") return ir::IRType::Pointer(ir::IRType::I8());
    // Default to i64 for dynamic types
    if (!type_hint.empty()) {
        std::cerr << "[python-lowering] unresolved type hint '" << type_hint
                  << "'; defaulting to i64\n";
    }
    return ir::IRType::I64(true);
}

// ----------------------------------------------------------------------------
// Environment entry for variable tracking.
// ----------------------------------------------------------------------------
struct EnvEntry {
    Name value;
    ir::IRType type{ir::IRType::Invalid()};
    Name alloca_name;  // For mutable variables
    bool is_mutable{false};
};

// ----------------------------------------------------------------------------
// Loop context for break/continue handling.
// ----------------------------------------------------------------------------
struct LoopContext {
    ir::BasicBlock *continue_target{nullptr};
    ir::BasicBlock *break_target{nullptr};
};

// ----------------------------------------------------------------------------
// Class info for method lowering.
// ----------------------------------------------------------------------------
struct ClassInfo {
    std::string name;
    std::vector<std::string> methods;
    std::vector<std::string> fields;
};

// ----------------------------------------------------------------------------
// Main lowering context.
// ----------------------------------------------------------------------------
struct LoweringContext {
    ir::IRContext &ir_ctx;
    frontends::Diagnostics &diags;
    std::unordered_map<Name, EnvEntry> env;
    ir::IRBuilder builder;
    std::shared_ptr<ir::Function> fn;
    bool terminated{false};

    // Loop stack for break/continue
    std::stack<LoopContext> loop_stack;

    // Class info for current class being lowered
    std::unordered_map<std::string, ClassInfo> classes;
    std::string current_class;

    // Async context tracking
    bool in_async_function{false};

    // Temp counter for unique names
    size_t temp_counter{0};

    LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d)
        : ir_ctx(ctx), diags(d), builder(ctx) {}

    std::string NextTemp(const std::string &prefix = "tmp") {
        return prefix + "." + std::to_string(temp_counter++);
    }

    void SetInsertBlock(ir::BasicBlock *bb) {
        for (auto &block : fn->blocks) {
            if (block.get() == bb) {
                builder.SetInsertPoint(block);
                return;
            }
        }
    }
};

// ----------------------------------------------------------------------------
// Evaluation result.
// ----------------------------------------------------------------------------
struct EvalResult {
    Name value;
    ir::IRType type{ir::IRType::Invalid()};

    bool IsValid() const { return type.kind != ir::IRTypeKind::kInvalid; }
    static EvalResult Invalid() { return {}; }
};

// Forward declarations
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc);
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);
bool LowerFunction(const FunctionDef &fn, LoweringContext &lc);
bool LowerClass(const ClassDef &cls, LoweringContext &lc);

// ----------------------------------------------------------------------------
// Literal parsing helpers.
// ----------------------------------------------------------------------------
bool IsIntegerLiteral(const std::string &text, long long *out) {
    if (text.empty()) return false;
    // Handle Python keywords as literals
    if (text == "True") { if (out) *out = 1; return true; }
    if (text == "False") { if (out) *out = 0; return true; }
    if (text == "None") { if (out) *out = 0; return true; }
    
    char *end = nullptr;
    long long v = std::strtoll(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0') return false;
    if (out) *out = v;
    return true;
}

bool IsFloatLiteral(const std::string &text, double *out) {
    if (text.empty()) return false;
    char *end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return false;
    if (out) *out = v;
    return true;
}

// ----------------------------------------------------------------------------
// Make literals.
// ----------------------------------------------------------------------------
EvalResult MakeLiteral(long long v, LoweringContext &lc) {
    auto lit = lc.builder.MakeLiteral(v, lc.NextTemp("lit"));
    return {lit->name, ir::IRType::I64(true)};
}

EvalResult MakeFloatLiteral(double v, LoweringContext &lc) {
    auto lit = lc.builder.MakeLiteral(v, lc.NextTemp("flit"));
    return {lit->name, ir::IRType::F64()};
}

EvalResult MakeBoolLiteral(bool v, LoweringContext &lc) {
    auto lit = lc.builder.MakeLiteral(v ? 1LL : 0LL, lc.NextTemp("bool"));
    lit->type = ir::IRType::I1();
    return {lit->name, ir::IRType::I1()};
}

// ----------------------------------------------------------------------------
// Name evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalName(const std::shared_ptr<Identifier> &name, LoweringContext &lc) {
    auto it = lc.env.find(name->name);
    if (it == lc.env.end()) {
        // Check for built-in names
        if (name->name == "print" || name->name == "len" || name->name == "range" ||
            name->name == "int" || name->name == "float" || name->name == "str" ||
            name->name == "list" || name->name == "dict" || name->name == "set" ||
            name->name == "type" || name->name == "isinstance" || name->name == "hasattr" ||
            name->name == "bool" || name->name == "tuple" || name->name == "object" ||
            name->name == "abs" || name->name == "min" || name->name == "max" ||
            name->name == "sum" || name->name == "sorted" || name->name == "reversed" ||
            name->name == "enumerate" || name->name == "zip" || name->name == "map" ||
            name->name == "filter" || name->name == "open" || name->name == "input" ||
            name->name == "super" || name->name == "getattr" || name->name == "setattr" ||
            name->name == "issubclass" || name->name == "property" ||
            name->name == "classmethod" || name->name == "staticmethod" ||
            // Exception types
            name->name == "ValueError" || name->name == "TypeError" ||
            name->name == "RuntimeError" || name->name == "KeyError" ||
            name->name == "IndexError" || name->name == "AttributeError" ||
            name->name == "StopIteration" || name->name == "Exception" ||
            name->name == "IOError" || name->name == "OSError" ||
            name->name == "FileNotFoundError" || name->name == "NameError" ||
            name->name == "ZeroDivisionError" || name->name == "NotImplementedError" ||
            // Constants
            name->name == "None" || name->name == "True" || name->name == "False") {
            // Return a placeholder for built-in functions/types
            return {name->name, ir::IRType::I64(true)};
        }
        lc.diags.Report(name->loc, "Undefined name: " + name->name);
        return EvalResult::Invalid();
    }
    // Load from alloca if mutable
    if (it->second.is_mutable && !it->second.alloca_name.empty()) {
        auto load = lc.builder.MakeLoad(it->second.alloca_name, it->second.type, lc.NextTemp("load"));
        return {load->name, it->second.type};
    }
    return {it->second.value, it->second.type};
}

// ----------------------------------------------------------------------------
// Binary operator mapping.
// ----------------------------------------------------------------------------
ir::BinaryInstruction::Op MapBinOp(const std::string &op, bool is_float = false) {
    if (op == "+") return is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
    if (op == "-") return is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
    if (op == "*") return is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
    if (op == "/") return is_float ? ir::BinaryInstruction::Op::kFDiv : ir::BinaryInstruction::Op::kSDiv;
    if (op == "//") return ir::BinaryInstruction::Op::kSDiv;  // Floor division
    if (op == "%") return is_float ? ir::BinaryInstruction::Op::kFRem : ir::BinaryInstruction::Op::kSRem;
    if (op == "**") return ir::BinaryInstruction::Op::kMul;  // Simplified power
    if (op == "&") return ir::BinaryInstruction::Op::kAnd;
    if (op == "|") return ir::BinaryInstruction::Op::kOr;
    if (op == "^") return ir::BinaryInstruction::Op::kXor;
    if (op == "<<") return ir::BinaryInstruction::Op::kShl;
    if (op == ">>") return ir::BinaryInstruction::Op::kAShr;
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
            return true;
        default:
            return false;
    }
}

// ----------------------------------------------------------------------------
// Binary expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalBinOp(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
    // Handle logical and/or with short-circuit evaluation and proper PHI nodes
    if (bin->op == "and" || bin->op == "or") {
        auto lhs = EvalExpr(bin->left, lc);
        if (!lhs.IsValid()) return EvalResult::Invalid();

        auto *rhs_block = lc.fn->CreateBlock(bin->op == "and" ? "and.rhs" : "or.rhs");
        auto *merge_block = lc.fn->CreateBlock(bin->op == "and" ? "and.end" : "or.end");
        
        // Get current block for PHI incoming
        auto *lhs_block = lc.builder.GetInsertPoint().get();

        if (bin->op == "and") {
            // and: if lhs is false, result is lhs; otherwise evaluate rhs
            lc.builder.MakeCondBranch(lhs.value, rhs_block, merge_block);
        } else {
            // or: if lhs is true, result is lhs; otherwise evaluate rhs
            lc.builder.MakeCondBranch(lhs.value, merge_block, rhs_block);
        }

        lc.SetInsertBlock(rhs_block);
        auto rhs = EvalExpr(bin->right, lc);
        if (!rhs.IsValid()) return EvalResult::Invalid();
        
        auto *rhs_end_block = lc.builder.GetInsertPoint().get();
        lc.builder.MakeBranch(merge_block);

        lc.SetInsertBlock(merge_block);
        
        // Create PHI node to select between lhs and rhs results
        // For 'and': if lhs was false, use lhs (false); else use rhs
        // For 'or': if lhs was true, use lhs (true); else use rhs
        ir::IRType result_type = lhs.type;
        if (result_type.kind == ir::IRTypeKind::kInvalid) {
            result_type = ir::IRType::I64(true);
        }
        
        std::vector<std::pair<ir::BasicBlock*, std::string>> phi_incomings = {
            {lhs_block, lhs.value},
            {rhs_end_block, rhs.value}
        };
        
        auto phi = lc.builder.MakePhi(result_type, phi_incomings, lc.NextTemp("logic"));
        return {phi->name, result_type};
    }

    auto lhs = EvalExpr(bin->left, lc);
    auto rhs = EvalExpr(bin->right, lc);
    if (!lhs.IsValid() || !rhs.IsValid()) return EvalResult::Invalid();

    bool is_float = (lhs.type.kind == ir::IRTypeKind::kF64 || rhs.type.kind == ir::IRTypeKind::kF64);
    ir::BinaryInstruction::Op op = MapBinOp(bin->op, is_float);
    auto inst = lc.builder.MakeBinary(op, lhs.value, rhs.value, lc.NextTemp("bin"));

    if (IsCmpOp(op)) {
        inst->type = ir::IRType::I1();
    } else {
        inst->type = is_float ? ir::IRType::F64() : lhs.type;
    }
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Unary expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalUnaryOp(const std::shared_ptr<UnaryExpression> &un, LoweringContext &lc) {
    auto operand = EvalExpr(un->operand, lc);
    if (!operand.IsValid()) return EvalResult::Invalid();

    if (un->op == "not") {
        // Boolean not: compare with 0
        auto zero = MakeLiteral(0, lc);
        auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, 
                                          operand.value, zero.value, lc.NextTemp("not"));
        inst->type = ir::IRType::I1();
        return {inst->name, ir::IRType::I1()};
    }
    if (un->op == "-") {
        auto zero = MakeLiteral(0, lc);
        bool is_float = operand.type.kind == ir::IRTypeKind::kF64;
        auto op = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
        auto inst = lc.builder.MakeBinary(op, zero.value, operand.value, lc.NextTemp("neg"));
        inst->type = operand.type;
        return {inst->name, operand.type};
    }
    if (un->op == "+") {
        return operand;  // Unary plus is a no-op
    }
    if (un->op == "~") {
        // Bitwise not: XOR with -1
        auto neg_one = MakeLiteral(-1, lc);
        auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor,
                                          operand.value, neg_one.value, lc.NextTemp("bnot"));
        inst->type = operand.type;
        return {inst->name, operand.type};
    }

    lc.diags.Report(un->loc, "Unsupported unary operator: " + un->op);
    return EvalResult::Invalid();
}

// ----------------------------------------------------------------------------
// Call expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalCall(const std::shared_ptr<CallExpression> &call, LoweringContext &lc) {
    std::vector<std::string> args;
    std::vector<ir::IRType> arg_types;
    for (const auto &arg : call->args) {
        if (arg.is_star) {
            // *args unpacking: evaluate the iterable and emit a runtime helper
            // that expands it into individual arguments.  For IR purposes we
            // pass the iterable pointer directly; the runtime call convention
            // treats it as a variadic pack.
            auto ev = EvalExpr(arg.value, lc);
            if (!ev.IsValid()) return EvalResult::Invalid();
            auto expanded = lc.builder.MakeCall("__py_unpack_args",
                                                {ev.value}, ir::IRType::Pointer(ir::IRType::I8()),
                                                lc.NextTemp("star"));
            args.push_back(expanded->name);
            arg_types.push_back(expanded->type);
            continue;
        }
        if (arg.is_kwstar) {
            // **kwargs unpacking: similar treatment — pass the dict to a
            // runtime helper that merges keyword arguments.
            auto ev = EvalExpr(arg.value, lc);
            if (!ev.IsValid()) return EvalResult::Invalid();
            auto expanded = lc.builder.MakeCall("__py_unpack_kwargs",
                                                {ev.value}, ir::IRType::Pointer(ir::IRType::I8()),
                                                lc.NextTemp("kwstar"));
            args.push_back(expanded->name);
            arg_types.push_back(expanded->type);
            continue;
        }
        auto ev = EvalExpr(arg.value, lc);
        if (!ev.IsValid()) return EvalResult::Invalid();
        args.push_back(ev.value);
        arg_types.push_back(ev.type);
    }

    std::string callee_name;
    if (auto name = std::dynamic_pointer_cast<Identifier>(call->callee)) {
        callee_name = name->name;
    } else if (auto attr = std::dynamic_pointer_cast<AttributeExpression>(call->callee)) {
        // Method call: obj.method(...)
        auto obj = EvalExpr(attr->object, lc);
        if (!obj.IsValid()) return EvalResult::Invalid();
        // Mangle method name
        callee_name = "__py_method_" + attr->attribute;
        args.insert(args.begin(), obj.value);
        arg_types.insert(arg_types.begin(), obj.type);
    } else {
        // Indirect call through expression
        auto callee = EvalExpr(call->callee, lc);
        if (!callee.IsValid()) return EvalResult::Invalid();
        callee_name = callee.value;
    }

    // Handle built-in functions
    if (callee_name == "print") {
        // Generate call to runtime print function
        auto inst = lc.builder.MakeCall("__py_print", args, ir::IRType::Void(), "");
        return {"", ir::IRType::Void()};
    }
    if (callee_name == "len") {
        auto inst = lc.builder.MakeCall("__py_len", args, ir::IRType::I64(true), lc.NextTemp("len"));
        return {inst->name, ir::IRType::I64(true)};
    }
    if (callee_name == "range") {
        // Range returns an iterator object with start, stop, step, and current
        // __py_range creates an iterator object that can be used with for loops
        // The iterator protocol: __iter__ returns self, __next__ returns next value or raises StopIteration
        
        ir::IRType range_iter_type = ir::IRType::Pointer(ir::IRType::I64(true));
        
        std::string range_fn;
        if (args.size() == 1) {
            // range(stop) -> range(0, stop, 1)
            range_fn = "__py_range_1";
        } else if (args.size() == 2) {
            // range(start, stop) -> range(start, stop, 1)
            range_fn = "__py_range_2";
        } else if (args.size() == 3) {
            // range(start, stop, step)
            range_fn = "__py_range_3";
        } else {
            lc.diags.Report(call->loc, "range() requires 1-3 arguments");
            return EvalResult::Invalid();
        }
        
        auto inst = lc.builder.MakeCall(range_fn, args, range_iter_type, lc.NextTemp("range"));
        return {inst->name, range_iter_type};
    }

    auto inst = lc.builder.MakeCall(callee_name, args, ir::IRType::I64(true), lc.NextTemp("call"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Attribute expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalAttribute(const std::shared_ptr<AttributeExpression> &attr, LoweringContext &lc) {
    auto obj = EvalExpr(attr->object, lc);
    if (!obj.IsValid()) return EvalResult::Invalid();

    // Generate a field access call
    std::string getter_name = "__py_getattr_" + attr->attribute;
    auto inst = lc.builder.MakeCall(getter_name, {obj.value}, ir::IRType::I64(true), lc.NextTemp("attr"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Index expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalIndex(const std::shared_ptr<IndexExpression> &idx, LoweringContext &lc) {
    auto obj = EvalExpr(idx->object, lc);
    auto index = EvalExpr(idx->index, lc);
    if (!obj.IsValid() || !index.IsValid()) return EvalResult::Invalid();

    auto inst = lc.builder.MakeCall("__py_getitem", {obj.value, index.value}, 
                                    ir::IRType::I64(true), lc.NextTemp("idx"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Slice expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalSlice(const std::shared_ptr<SliceExpression> &slice, LoweringContext &lc) {
    auto start = slice->start ? EvalExpr(slice->start, lc) : MakeLiteral(0, lc);
    auto stop = slice->stop ? EvalExpr(slice->stop, lc) : MakeLiteral(-1, lc);
    auto step = slice->step ? EvalExpr(slice->step, lc) : MakeLiteral(1, lc);

    if (!start.IsValid() || !stop.IsValid() || !step.IsValid()) return EvalResult::Invalid();

    auto inst = lc.builder.MakeCall("__py_slice", {start.value, stop.value, step.value},
                                    ir::IRType::I64(true), lc.NextTemp("slice"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Tuple expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalTuple(const std::shared_ptr<TupleExpression> &tup, LoweringContext &lc) {
    std::vector<std::string> elems;
    for (const auto &e : tup->elements) {
        auto ev = EvalExpr(e, lc);
        if (!ev.IsValid()) return EvalResult::Invalid();
        elems.push_back(ev.value);
    }

    // Create tuple via runtime call
    auto inst = lc.builder.MakeCall("__py_make_tuple", elems, ir::IRType::I64(true), lc.NextTemp("tuple"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// List expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalList(const std::shared_ptr<ListExpression> &lst, LoweringContext &lc) {
    std::vector<std::string> elems;
    for (const auto &e : lst->elements) {
        auto ev = EvalExpr(e, lc);
        if (!ev.IsValid()) return EvalResult::Invalid();
        elems.push_back(ev.value);
    }

    auto inst = lc.builder.MakeCall("__py_make_list", elems, ir::IRType::I64(true), lc.NextTemp("list"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Dict expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalDict(const std::shared_ptr<DictExpression> &dict, LoweringContext &lc) {
    std::vector<std::string> args;
    for (const auto &[k, v] : dict->items) {
        auto key = EvalExpr(k, lc);
        auto val = EvalExpr(v, lc);
        if (!key.IsValid() || !val.IsValid()) return EvalResult::Invalid();
        args.push_back(key.value);
        args.push_back(val.value);
    }

    auto inst = lc.builder.MakeCall("__py_make_dict", args, ir::IRType::I64(true), lc.NextTemp("dict"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Set expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalSet(const std::shared_ptr<SetExpression> &set_expr, LoweringContext &lc) {
    std::vector<std::string> elems;
    for (const auto &e : set_expr->elements) {
        auto ev = EvalExpr(e, lc);
        if (!ev.IsValid()) return EvalResult::Invalid();
        elems.push_back(ev.value);
    }

    auto inst = lc.builder.MakeCall("__py_make_set", elems, ir::IRType::I64(true), lc.NextTemp("set"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Comprehension expression evaluation.
// Implements full loop unrolling for list/set/dict comprehensions.
// e.g. [x*2 for x in range(10) if x > 5] generates proper loop IR.
// ----------------------------------------------------------------------------
EvalResult EvalComprehension(const std::shared_ptr<ComprehensionExpression> &comp, LoweringContext &lc) {
    // Create a temporary list/set/dict container
    std::string result_name;
    ir::IRType result_type = ir::IRType::I64(true);

    switch (comp->kind) {
        case ComprehensionExpression::Kind::kList:
            result_name = lc.NextTemp("listcomp");
            break;
        case ComprehensionExpression::Kind::kSet:
            result_name = lc.NextTemp("setcomp");
            break;
        case ComprehensionExpression::Kind::kDict:
            result_name = lc.NextTemp("dictcomp");
            break;
        case ComprehensionExpression::Kind::kGenerator:
            result_name = lc.NextTemp("genexp");
            break;
    }

    // Create empty container
    std::string init_fn;
    std::string append_fn;
    switch (comp->kind) {
        case ComprehensionExpression::Kind::kList:
        case ComprehensionExpression::Kind::kGenerator:
            init_fn = "__py_make_list";
            append_fn = "__py_list_append";
            break;
        case ComprehensionExpression::Kind::kSet:
            init_fn = "__py_make_set";
            append_fn = "__py_set_add";
            break;
        case ComprehensionExpression::Kind::kDict:
            init_fn = "__py_make_dict";
            append_fn = "__py_dict_setitem";
            break;
    }

    auto container = lc.builder.MakeCall(init_fn, {}, result_type, result_name);

    if (comp->clauses.empty()) {
        return {container->name, result_type};
    }

    // Generate nested loops for each comprehension clause
    // Stack to track loop blocks for proper nesting
    struct LoopInfo {
        ir::BasicBlock *header;
        ir::BasicBlock *body;
        ir::BasicBlock *end;
        std::string iter_var;
        std::string iter_obj;
    };
    std::vector<LoopInfo> loops;

    // Process each clause (for x in iterable if condition)
    // Note: Comprehension struct uses 'ifs' for conditions and 'target'/'iterable' for loop
    for (const auto &clause : comp->clauses) {
        auto iterable = EvalExpr(clause.iterable, lc);
        if (!iterable.IsValid()) return EvalResult::Invalid();

        // Get iterator from iterable
        auto iter = lc.builder.MakeCall("__py_iter", {iterable.value}, 
                                        ir::IRType::Pointer(ir::IRType::I64(true)), 
                                        lc.NextTemp("iter"));

        // Create loop blocks
        auto *header = lc.fn->CreateBlock("comp.header");
        auto *body = lc.fn->CreateBlock("comp.body");
        auto *filter_block = clause.ifs.empty() ? body : lc.fn->CreateBlock("comp.filter");
        auto *end = lc.fn->CreateBlock("comp.end");

        lc.builder.MakeBranch(header);
        lc.SetInsertBlock(header);

        // Call __py_next to get next element; returns None when exhausted
        auto next_val = lc.builder.MakeCall("__py_next", {iter->name}, 
                                            ir::IRType::I64(true), 
                                            lc.NextTemp("next"));
        
        // Check if iterator is exhausted (returns special sentinel value)
        auto is_done = lc.builder.MakeCall("__py_iter_done", {next_val->name}, 
                                           ir::IRType::I1(), 
                                           lc.NextTemp("done"));
        
        lc.builder.MakeCondBranch(is_done->name, end, filter_block);

        // Bind loop variable
        std::string target_name;
        if (auto id = std::dynamic_pointer_cast<Identifier>(clause.target)) {
            target_name = id->name;
        } else {
            lc.diags.Report(comp->loc, "Only simple identifiers supported as comprehension targets");
            return EvalResult::Invalid();
        }

        // Store in filter block or body block
        lc.SetInsertBlock(filter_block);
        lc.env[target_name] = {next_val->name, ir::IRType::I64(true), "", false};

        // Evaluate filter conditions (ifs)
        if (!clause.ifs.empty()) {
            auto *current_block = filter_block;
            for (size_t i = 0; i < clause.ifs.size(); ++i) {
                auto cond = EvalExpr(clause.ifs[i], lc);
                if (!cond.IsValid()) return EvalResult::Invalid();
                
                if (i + 1 < clause.ifs.size()) {
                    // More conditions to check
                    auto *next_filter = lc.fn->CreateBlock("comp.filter");
                    lc.builder.MakeCondBranch(cond.value, next_filter, header);
                    lc.SetInsertBlock(next_filter);
                } else {
                    // Last condition, branch to body or back to header
                    lc.builder.MakeCondBranch(cond.value, body, header);
                }
            }
        }

        loops.push_back({header, body, end, target_name, iter->name});
        lc.SetInsertBlock(body);
    }

    // Now in innermost body block, evaluate the element expression and append
    // ComprehensionExpression uses 'elem' for element and 'key' for dict key
    if (comp->kind == ComprehensionExpression::Kind::kDict) {
        // Dict comprehension: key: value
        if (comp->key && comp->elem) {
            auto key = EvalExpr(comp->key, lc);
            auto value = EvalExpr(comp->elem, lc);
            if (!key.IsValid() || !value.IsValid()) return EvalResult::Invalid();
            lc.builder.MakeCall(append_fn, {container->name, key.value, value.value}, 
                                ir::IRType::Void(), "");
        }
    } else {
        // List/Set/Generator: single element
        auto elem = EvalExpr(comp->elem, lc);
        if (!elem.IsValid()) return EvalResult::Invalid();
        lc.builder.MakeCall(append_fn, {container->name, elem.value}, ir::IRType::Void(), "");
    }

    // Branch back to innermost header
    if (!loops.empty()) {
        lc.builder.MakeBranch(loops.back().header);
    }

    // Wire up end blocks - go from innermost to outermost
    for (int i = static_cast<int>(loops.size()) - 1; i >= 0; --i) {
        lc.SetInsertBlock(loops[i].end);
        if (i > 0) {
            // Continue to outer loop header
            lc.builder.MakeBranch(loops[i-1].header);
        }
        // else: outermost end block, will be set as final block below
    }

    // Final block after all loops
    auto *final_block = lc.fn->CreateBlock("comp.done");
    if (!loops.empty()) {
        lc.SetInsertBlock(loops[0].end);
        lc.builder.MakeBranch(final_block);
    }
    lc.SetInsertBlock(final_block);

    return {container->name, result_type};
}

// ----------------------------------------------------------------------------
// Lambda expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalLambda(const std::shared_ptr<LambdaExpression> &lambda, LoweringContext &lc) {
    // Create a nested function for the lambda
    std::string lambda_name = lc.NextTemp("lambda");

    // Build parameter list
    std::vector<std::pair<std::string, ir::IRType>> params;
    for (const auto &p : lambda->params) {
        params.push_back({p.name, ir::IRType::I64(true)});
    }

    // Save current state
    auto saved_fn = lc.fn;
    auto saved_env = lc.env;
    bool saved_term = lc.terminated;

    // Create lambda function
    lc.fn = lc.ir_ctx.CreateFunction(lambda_name, ir::IRType::I64(true), params);
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    lc.SetInsertBlock(entry);

    lc.env.clear();
    for (const auto &p : params) {
        lc.env[p.first] = {p.first, p.second, "", false};
    }
    lc.terminated = false;

    // Evaluate body and return result
    auto result = EvalExpr(lambda->body, lc);
    if (result.IsValid()) {
        lc.builder.MakeReturn(result.value);
    } else {
        lc.builder.MakeReturn("");
    }

    // Restore state
    lc.fn = saved_fn;
    lc.env = saved_env;
    lc.terminated = saved_term;

    // Return function pointer
    return {lambda_name, ir::IRType::I64(true)};
}

// ----------------------------------------------------------------------------
// Await expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalAwait(const std::shared_ptr<AwaitExpression> &await, LoweringContext &lc) {
    if (!lc.in_async_function) {
        lc.diags.Report(await->loc, "'await' outside async function");
        return EvalResult::Invalid();
    }

    auto awaitable = EvalExpr(await->value, lc);
    if (!awaitable.IsValid()) return EvalResult::Invalid();

    // Generate await intrinsic call
    auto inst = lc.builder.MakeCall("__py_await", {awaitable.value}, 
                                    ir::IRType::I64(true), lc.NextTemp("await"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Yield expression evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalYield(const std::shared_ptr<YieldExpression> &yield, LoweringContext &lc) {
    EvalResult val;
    if (yield->value) {
        val = EvalExpr(yield->value, lc);
        if (!val.IsValid()) return EvalResult::Invalid();
    } else {
        val = MakeLiteral(0, lc);  // yield None
    }

    std::string fn_name = yield->is_from ? "__py_yield_from" : "__py_yield";
    auto inst = lc.builder.MakeCall(fn_name, {val.value}, ir::IRType::I64(true), lc.NextTemp("yield"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Named expression (walrus operator) evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalNamedExpr(const std::shared_ptr<NamedExpression> &named, LoweringContext &lc) {
    auto val = EvalExpr(named->value, lc);
    if (!val.IsValid()) return EvalResult::Invalid();

    // Assign to target
    if (auto id = std::dynamic_pointer_cast<Identifier>(named->target)) {
        lc.env[id->name] = {val.value, val.type, "", false};
    }

    return val;
}

// ----------------------------------------------------------------------------
// Formatted string evaluation.
// ----------------------------------------------------------------------------
EvalResult EvalFormattedString(const std::shared_ptr<FormattedString> &fstr, LoweringContext &lc) {
    std::vector<std::string> parts;
    for (const auto &part : fstr->parts) {
        if (part.is_literal) {
            auto str_ptr = lc.builder.MakeStringLiteral(part.literal, lc.NextTemp("fstr.lit"));
            parts.push_back(str_ptr);
        } else {
            auto val = EvalExpr(part.expr, lc);
            if (!val.IsValid()) return EvalResult::Invalid();
            // Convert to string if needed
            auto str_val = lc.builder.MakeCall("__py_str", {val.value}, 
                                               ir::IRType::Pointer(ir::IRType::I8()), 
                                               lc.NextTemp("fstr.val"));
            parts.push_back(str_val->name);
        }
    }

    // Concatenate all parts
    auto inst = lc.builder.MakeCall("__py_str_concat", parts, 
                                    ir::IRType::Pointer(ir::IRType::I8()), 
                                    lc.NextTemp("fstr"));
    return {inst->name, inst->type};
}

// ----------------------------------------------------------------------------
// Main expression evaluator.
// ----------------------------------------------------------------------------
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
    if (!expr) return EvalResult::Invalid();

    // Literal
    if (auto literal = std::dynamic_pointer_cast<Literal>(expr)) {
        if (literal->is_string) {
            auto str_ptr = lc.builder.MakeStringLiteral(literal->value, lc.NextTemp("str"));
            return {str_ptr, ir::IRType::Pointer(ir::IRType::I8())};
        }
        long long iv{};
        if (IsIntegerLiteral(literal->value, &iv)) {
            return MakeLiteral(iv, lc);
        }
        double fv{};
        if (IsFloatLiteral(literal->value, &fv)) {
            return MakeFloatLiteral(fv, lc);
        }
        lc.diags.Report(literal->loc, "Invalid literal: " + literal->value);
        return EvalResult::Invalid();
    }

    // Identifier
    if (auto name = std::dynamic_pointer_cast<Identifier>(expr)) {
        return EvalName(name, lc);
    }

    // Binary expression
    if (auto binop = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return EvalBinOp(binop, lc);
    }

    // Unary expression
    if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
        return EvalUnaryOp(unary, lc);
    }

    // Call expression
    if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
        return EvalCall(call, lc);
    }

    // Attribute expression
    if (auto attr = std::dynamic_pointer_cast<AttributeExpression>(expr)) {
        return EvalAttribute(attr, lc);
    }

    // Index expression
    if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
        return EvalIndex(idx, lc);
    }

    // Slice expression
    if (auto slice = std::dynamic_pointer_cast<SliceExpression>(expr)) {
        return EvalSlice(slice, lc);
    }

    // Tuple expression
    if (auto tup = std::dynamic_pointer_cast<TupleExpression>(expr)) {
        return EvalTuple(tup, lc);
    }

    // List expression
    if (auto lst = std::dynamic_pointer_cast<ListExpression>(expr)) {
        return EvalList(lst, lc);
    }

    // Dict expression
    if (auto dict = std::dynamic_pointer_cast<DictExpression>(expr)) {
        return EvalDict(dict, lc);
    }

    // Set expression
    if (auto set_expr = std::dynamic_pointer_cast<SetExpression>(expr)) {
        return EvalSet(set_expr, lc);
    }

    // Comprehension expression
    if (auto comp = std::dynamic_pointer_cast<ComprehensionExpression>(expr)) {
        return EvalComprehension(comp, lc);
    }

    // Lambda expression
    if (auto lambda = std::dynamic_pointer_cast<LambdaExpression>(expr)) {
        return EvalLambda(lambda, lc);
    }

    // Await expression
    if (auto await = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
        return EvalAwait(await, lc);
    }

    // Yield expression
    if (auto yield = std::dynamic_pointer_cast<YieldExpression>(expr)) {
        return EvalYield(yield, lc);
    }

    // Named expression (:=)
    if (auto named = std::dynamic_pointer_cast<NamedExpression>(expr)) {
        return EvalNamedExpr(named, lc);
    }

    // Formatted string (f-string)
    if (auto fstr = std::dynamic_pointer_cast<FormattedString>(expr)) {
        return EvalFormattedString(fstr, lc);
    }

    lc.diags.Report(expr->loc, "Unsupported expression type in lowering");
    return EvalResult::Invalid();
}

// ----------------------------------------------------------------------------
// Statement lowering functions.
// ----------------------------------------------------------------------------

bool LowerReturn(const std::shared_ptr<ReturnStatement> &ret, LoweringContext &lc) {
    if (lc.terminated) return true;
    EvalResult v;
    if (ret->value) {
        v = EvalExpr(ret->value, lc);
        if (!v.IsValid()) {
            lc.builder.MakeReturn("");
            lc.terminated = true;
            return true;
        }
    }
    lc.builder.MakeReturn(v.value);
    lc.terminated = true;
    return true;
}

bool LowerAssign(const std::shared_ptr<Assignment> &assign, LoweringContext &lc) {
    if (assign->targets.empty()) return false;

    // Handle annotated assignment without value (just type declaration)
    if (!assign->value && assign->annotation) {
        for (const auto &target : assign->targets) {
            if (auto name = std::dynamic_pointer_cast<Identifier>(target)) {
                ir::IRType ty = ir::IRType::I64(true);
                if (auto ann_id = std::dynamic_pointer_cast<Identifier>(assign->annotation)) {
                    ty = ToIRType(ann_id->name);
                }
                lc.env[name->name] = {"", ty, "", false};
            }
        }
        return true;
    }

    if (!assign->value) return false;

    auto result = EvalExpr(assign->value, lc);
    if (!result.IsValid()) return false;

    // Handle augmented assignment
    if (assign->op != "=") {
        if (assign->targets.size() != 1) {
            lc.diags.Report(assign->loc, "Augmented assignment requires single target");
            return false;
        }
        auto target = assign->targets[0];
        if (auto name = std::dynamic_pointer_cast<Identifier>(target)) {
            auto current = EvalName(name, lc);
            if (!current.IsValid()) return false;

            std::string bin_op;
            if (assign->op == "+=") bin_op = "+";
            else if (assign->op == "-=") bin_op = "-";
            else if (assign->op == "*=") bin_op = "*";
            else if (assign->op == "/=") bin_op = "/";
            else if (assign->op == "//=") bin_op = "//";
            else if (assign->op == "%=") bin_op = "%";
            else if (assign->op == "**=") bin_op = "**";
            else if (assign->op == "&=") bin_op = "&";
            else if (assign->op == "|=") bin_op = "|";
            else if (assign->op == "^=") bin_op = "^";
            else if (assign->op == "<<=") bin_op = "<<";
            else if (assign->op == ">>=") bin_op = ">>";
            else {
                lc.diags.Report(assign->loc, "Unknown augmented assignment operator: " + assign->op);
                return false;
            }

            bool is_float = (current.type.kind == ir::IRTypeKind::kF64 || 
                            result.type.kind == ir::IRTypeKind::kF64);
            auto op = MapBinOp(bin_op, is_float);
            auto inst = lc.builder.MakeBinary(op, current.value, result.value, lc.NextTemp("aug"));
            inst->type = current.type;
            lc.env[name->name] = {inst->name, inst->type, "", false};
            return true;
        }
    }

    // Simple assignment
    for (const auto &target : assign->targets) {
        if (auto name = std::dynamic_pointer_cast<Identifier>(target)) {
            lc.env[name->name] = {result.value, result.type, "", false};
        } else if (auto tup = std::dynamic_pointer_cast<TupleExpression>(target)) {
            // Tuple unpacking
            for (size_t i = 0; i < tup->elements.size(); ++i) {
                if (auto elem_name = std::dynamic_pointer_cast<Identifier>(tup->elements[i])) {
                    auto idx_val = MakeLiteral(static_cast<long long>(i), lc);
                    auto item = lc.builder.MakeCall("__py_getitem", {result.value, idx_val.value},
                                                    ir::IRType::I64(true), lc.NextTemp("unpack"));
                    lc.env[elem_name->name] = {item->name, item->type, "", false};
                }
            }
        } else if (auto attr = std::dynamic_pointer_cast<AttributeExpression>(target)) {
            // Attribute assignment: obj.attr = value
            auto obj = EvalExpr(attr->object, lc);
            if (!obj.IsValid()) return false;
            lc.builder.MakeCall("__py_setattr_" + attr->attribute, {obj.value, result.value},
                               ir::IRType::Void(), "");
        } else if (auto idx = std::dynamic_pointer_cast<IndexExpression>(target)) {
            // Index assignment: obj[key] = value
            auto obj = EvalExpr(idx->object, lc);
            auto key = EvalExpr(idx->index, lc);
            if (!obj.IsValid() || !key.IsValid()) return false;
            lc.builder.MakeCall("__py_setitem", {obj.value, key.value, result.value},
                               ir::IRType::Void(), "");
        } else {
            lc.diags.Report(assign->loc, "Unsupported assignment target type");
            return false;
        }
    }
    return true;
}

bool LowerIf(const std::shared_ptr<IfStatement> &if_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;

    auto cond = EvalExpr(if_stmt->condition, lc);
    if (!cond.IsValid()) return false;

    auto *then_block = lc.fn->CreateBlock("if.then");
    auto *else_block = if_stmt->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
    auto *merge_block = lc.fn->CreateBlock("if.end");

    // Save environment state before branches for PHI generation
    std::unordered_map<std::string, EnvEntry> env_before = lc.env;

    lc.builder.MakeCondBranch(cond.value, then_block,
                              else_block ? else_block : merge_block);
    lc.terminated = false;

    // Then block
    lc.SetInsertBlock(then_block);
    lc.env = env_before;  // Start with same environment
    bool then_term = false;
    for (auto &s : if_stmt->then_body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) {
            then_term = true;
            break;
        }
    }
    auto *then_end_block = lc.builder.GetInsertPoint().get();
    std::unordered_map<std::string, EnvEntry> env_after_then = lc.env;
    if (!then_term) {
        lc.builder.MakeBranch(merge_block);
    }

    // Else block
    bool else_term = false;
    ir::BasicBlock *else_end_block = nullptr;
    std::unordered_map<std::string, EnvEntry> env_after_else = env_before;
    
    if (else_block) {
        lc.SetInsertBlock(else_block);
        lc.env = env_before;  // Reset to same starting environment
        lc.terminated = false;
        for (auto &s : if_stmt->else_body) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) {
                else_term = true;
                break;
            }
        }
        else_end_block = lc.builder.GetInsertPoint().get();
        env_after_else = lc.env;
        if (!else_term) {
            lc.builder.MakeBranch(merge_block);
        }
    }

    lc.SetInsertBlock(merge_block);
    lc.terminated = then_term && (else_term || !else_block);

    // Generate PHI nodes for variables modified in either branch
    // Only needed if both branches can reach merge block
    if (!then_term || (!else_term && else_block)) {
        std::set<std::string> modified_vars;
        
        // Find variables modified in then branch
        for (const auto &[name, info] : env_after_then) {
            auto it = env_before.find(name);
            if (it == env_before.end() || it->second.value != info.value) {
                modified_vars.insert(name);
            }
        }
        
        // Find variables modified in else branch
        for (const auto &[name, info] : env_after_else) {
            auto it = env_before.find(name);
            if (it == env_before.end() || it->second.value != info.value) {
                modified_vars.insert(name);
            }
        }

        // Generate PHI for each modified variable
        for (const auto &var_name : modified_vars) {
            std::string then_val, else_val;
            ir::IRType var_type = ir::IRType::I64(true);
            
            // Get value from then branch
            auto then_it = env_after_then.find(var_name);
            if (then_it != env_after_then.end()) {
                then_val = then_it->second.value;
                var_type = then_it->second.type;
            } else {
                auto before_it = env_before.find(var_name);
                then_val = before_it != env_before.end() ? before_it->second.value : "";
            }
            
            // Get value from else branch (or original if no else)
            auto else_it = env_after_else.find(var_name);
            if (else_it != env_after_else.end()) {
                else_val = else_it->second.value;
            } else {
                auto before_it = env_before.find(var_name);
                else_val = before_it != env_before.end() ? before_it->second.value : "";
            }
            
            // Only create PHI if both values are valid and different
            if (!then_val.empty() && !else_val.empty() && then_val != else_val) {
                ir::BasicBlock *then_pred = then_term ? nullptr : then_end_block;
                ir::BasicBlock *else_pred = (else_term || !else_block) ? nullptr 
                                            : (else_end_block ? else_end_block : merge_block);
                
                // Create PHI with incomings from reachable predecessors
                std::vector<std::pair<ir::BasicBlock*, std::string>> phi_incomings;
                if (then_pred && !then_term) {
                    phi_incomings.push_back({then_pred, then_val});
                }
                if (else_block && !else_term && else_pred) {
                    phi_incomings.push_back({else_pred, else_val});
                } else if (!else_block && !then_term) {
                    // No else branch: use original value from entry
                    auto *entry_pred = lc.fn->entry;
                    phi_incomings.push_back({entry_pred, else_val});
                }
                
                if (phi_incomings.size() > 1) {
                    auto phi = lc.builder.MakePhi(var_type, phi_incomings, lc.NextTemp("phi"));
                    lc.env[var_name] = {phi->name, var_type, "", false};
                } else if (!phi_incomings.empty()) {
                    // Only one incoming, use directly
                    lc.env[var_name] = {phi_incomings[0].second, var_type, "", false};
                }
            } else if (!then_val.empty()) {
                // Same value, just update environment
                lc.env[var_name] = {then_val, var_type, "", false};
            }
        }
    }

    return true;
}

bool LowerWhile(const std::shared_ptr<WhileStatement> &while_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;

    auto *cond_block = lc.fn->CreateBlock("while.cond");
    auto *body_block = lc.fn->CreateBlock("while.body");
    auto *exit_block = lc.fn->CreateBlock("while.end");

    // Save environment before loop for PHI generation
    std::unordered_map<std::string, EnvEntry> env_before = lc.env;
    auto *entry_block = lc.builder.GetInsertPoint().get();

    // Push loop context for break/continue
    lc.loop_stack.push({cond_block, exit_block});

    lc.builder.MakeBranch(cond_block);
    lc.SetInsertBlock(cond_block);
    lc.terminated = false;

    // Create placeholder PHI nodes for variables that might be modified in loop
    // These will be updated after processing the loop body
    std::vector<std::pair<std::string, std::shared_ptr<ir::PhiInstruction>>> loop_phis;

    auto cond = EvalExpr(while_stmt->condition, lc);
    if (!cond.IsValid()) {
        lc.loop_stack.pop();
        return false;
    }

    lc.builder.MakeCondBranch(cond.value, body_block, exit_block);

    lc.SetInsertBlock(body_block);
    lc.terminated = false;
    
    // Process loop body
    for (auto &s : while_stmt->body) {
        if (!LowerStmt(s, lc)) {
            lc.loop_stack.pop();
            return false;
        }
        if (lc.terminated) break;
    }
    
    auto *body_end_block = lc.builder.GetInsertPoint().get();
    std::unordered_map<std::string, EnvEntry> env_after_body = lc.env;
    
    if (!lc.terminated) {
        lc.builder.MakeBranch(cond_block);
    }

    // Now go back to cond_block and create proper PHI nodes for modified variables
    lc.SetInsertBlock(cond_block);
    
    for (const auto &[name, info] : env_after_body) {
        auto it = env_before.find(name);
        if (it != env_before.end() && it->second.value != info.value) {
            // Variable was modified in loop body
            std::vector<std::pair<ir::BasicBlock*, std::string>> phi_incomings = {
                {entry_block, it->second.value},   // Value before loop
                {body_end_block, info.value}       // Value after loop iteration
            };
            
            auto phi = lc.builder.MakePhi(info.type, phi_incomings, lc.NextTemp("loop.phi"));
            
            // Update the environment to use PHI in subsequent iterations
            lc.env[name] = {phi->name, info.type, "", false};
        }
    }

    lc.loop_stack.pop();
    lc.SetInsertBlock(exit_block);
    lc.terminated = false;
    return true;
}

bool LowerFor(const std::shared_ptr<ForStatement> &for_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;

    // Save environment before loop for PHI generation
    std::unordered_map<std::string, EnvEntry> env_before = lc.env;
    auto *entry_block = lc.builder.GetInsertPoint().get();

    // Evaluate iterable
    auto iterable = EvalExpr(for_stmt->iterable, lc);
    if (!iterable.IsValid()) return false;

    // Create iterator
    auto iter = lc.builder.MakeCall("__py_iter", {iterable.value}, 
                                    ir::IRType::I64(true), lc.NextTemp("iter"));

    auto *cond_block = lc.fn->CreateBlock("for.cond");
    auto *body_block = lc.fn->CreateBlock("for.body");
    auto *exit_block = lc.fn->CreateBlock("for.end");

    // Push loop context
    lc.loop_stack.push({cond_block, exit_block});

    lc.builder.MakeBranch(cond_block);
    lc.SetInsertBlock(cond_block);
    lc.terminated = false;

    // Check if iterator has next
    auto has_next = lc.builder.MakeCall("__py_iter_has_next", {iter->name},
                                        ir::IRType::I1(), lc.NextTemp("has_next"));
    lc.builder.MakeCondBranch(has_next->name, body_block, exit_block);

    lc.SetInsertBlock(body_block);
    lc.terminated = false;

    // Get next value and bind to target
    auto next_val = lc.builder.MakeCall("__py_iter_next", {iter->name},
                                        ir::IRType::I64(true), lc.NextTemp("next"));
    
    // Bind to loop variable(s)
    if (auto name = std::dynamic_pointer_cast<Identifier>(for_stmt->target)) {
        lc.env[name->name] = {next_val->name, next_val->type, "", false};
    } else if (auto tup = std::dynamic_pointer_cast<TupleExpression>(for_stmt->target)) {
        for (size_t i = 0; i < tup->elements.size(); ++i) {
            if (auto elem = std::dynamic_pointer_cast<Identifier>(tup->elements[i])) {
                auto idx_lit = MakeLiteral(static_cast<long long>(i), lc);
                auto item = lc.builder.MakeCall("__py_getitem", {next_val->name, idx_lit.value},
                                               ir::IRType::I64(true), lc.NextTemp("unpack"));
                lc.env[elem->name] = {item->name, item->type, "", false};
            }
        }
    }

    for (auto &s : for_stmt->body) {
        if (!LowerStmt(s, lc)) {
            lc.loop_stack.pop();
            return false;
        }
        if (lc.terminated) break;
    }
    
    auto *body_end_block = lc.builder.GetInsertPoint().get();
    std::unordered_map<std::string, EnvEntry> env_after_body = lc.env;
    
    if (!lc.terminated) {
        lc.builder.MakeBranch(cond_block);
    }

    // Create PHI nodes for variables modified in for loop body
    lc.SetInsertBlock(cond_block);
    
    for (const auto &[name, info] : env_after_body) {
        auto it = env_before.find(name);
        if (it != env_before.end() && it->second.value != info.value) {
            // Variable was modified in loop body (skip loop variable itself)
            if (auto target_id = std::dynamic_pointer_cast<Identifier>(for_stmt->target)) {
                if (name == target_id->name) continue;  // Skip loop variable
            }
            
            std::vector<std::pair<ir::BasicBlock*, std::string>> phi_incomings = {
                {entry_block, it->second.value},
                {body_end_block, info.value}
            };
            
            auto phi = lc.builder.MakePhi(info.type, phi_incomings, lc.NextTemp("for.phi"));
            lc.env[name] = {phi->name, info.type, "", false};
        }
    }

    lc.loop_stack.pop();
    lc.SetInsertBlock(exit_block);
    lc.terminated = false;
    return true;
}

bool LowerWith(const std::shared_ptr<WithStatement> &with_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;

    // Enter context managers
    std::vector<std::pair<std::string, std::string>> contexts;  // (mgr, exit_fn)
    for (const auto &item : with_stmt->items) {
        auto mgr = EvalExpr(item.context_expr, lc);
        if (!mgr.IsValid()) return false;

        // Call __enter__
        auto enter_result = lc.builder.MakeCall("__py_context_enter", {mgr.value},
                                                ir::IRType::I64(true), lc.NextTemp("enter"));
        contexts.push_back({mgr.value, enter_result->name});

        // Bind to variable if present
        if (item.optional_vars) {
            if (auto name = std::dynamic_pointer_cast<Identifier>(item.optional_vars)) {
                lc.env[name->name] = {enter_result->name, enter_result->type, "", false};
            }
        }
    }

    // Create blocks for try-finally structure
    auto *body_block = lc.fn->CreateBlock("with.body");
    auto *cleanup_block = lc.fn->CreateBlock("with.cleanup");
    auto *exit_block = lc.fn->CreateBlock("with.end");

    lc.builder.MakeBranch(body_block);
    lc.SetInsertBlock(body_block);
    lc.terminated = false;

    // Lower body
    bool body_term = false;
    for (auto &s : with_stmt->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) {
            body_term = true;
            break;
        }
    }
    if (!body_term) {
        lc.builder.MakeBranch(cleanup_block);
    }

    // Cleanup block: call __exit__ for all context managers (in reverse)
    lc.SetInsertBlock(cleanup_block);
    lc.terminated = false;
    for (auto it = contexts.rbegin(); it != contexts.rend(); ++it) {
        lc.builder.MakeCall("__py_context_exit", {it->first}, ir::IRType::Void(), "");
    }
    lc.builder.MakeBranch(exit_block);

    lc.SetInsertBlock(exit_block);
    lc.terminated = false;
    return true;
}

bool LowerTry(const std::shared_ptr<TryStatement> &try_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;

    auto *try_block = lc.fn->CreateBlock("try.body");
    auto *exit_block = lc.fn->CreateBlock("try.end");
    
    // Create handler blocks for each except clause
    std::vector<ir::BasicBlock *> handler_blocks;
    for (size_t i = 0; i < try_stmt->handlers.size(); ++i) {
        handler_blocks.push_back(lc.fn->CreateBlock("except." + std::to_string(i)));
    }
    
    auto *else_block = try_stmt->orelse.empty() ? nullptr : lc.fn->CreateBlock("try.else");
    auto *finally_block = try_stmt->finalbody.empty() ? nullptr : lc.fn->CreateBlock("try.finally");
    
    // Create landing pad block for exception dispatch
    auto *landing_pad = lc.fn->CreateBlock("try.landingpad");
    auto *unwind_block = lc.fn->CreateBlock("try.unwind");

    // Set up exception frame with proper landing pad registration
    // __py_push_exception_frame returns a context that includes:
    // - The landing pad address for setjmp/longjmp style unwinding
    // - Exception type information for RTTI-based dispatch
    auto exc_frame = lc.builder.MakeCall("__py_push_exception_frame", 
                                         {}, 
                                         ir::IRType::Pointer(ir::IRType::I64(true)), 
                                         lc.NextTemp("excframe"));
    
    // Check if we're entering fresh (0) or re-entering from exception (non-zero)
    auto setjmp_result = lc.builder.MakeCall("__py_setjmp", 
                                             {exc_frame->name}, 
                                             ir::IRType::I32(), 
                                             lc.NextTemp("setjmp"));
    
    // If setjmp returns 0, enter try block; otherwise go to landing pad
    auto is_exception = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpNe,
                                              setjmp_result->name, "0",
                                              lc.NextTemp("is_exc"));
    is_exception->type = ir::IRType::I1();
    
    lc.builder.MakeCondBranch(is_exception->name, landing_pad, try_block);
    
    // --- Try block ---
    lc.SetInsertBlock(try_block);
    lc.terminated = false;

    bool try_term = false;
    for (auto &s : try_stmt->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) {
            try_term = true;
            break;
        }
    }

    // Normal exit from try: pop exception frame and branch to else/finally/exit
    if (!try_term) {
        lc.builder.MakeCall("__py_pop_exception_frame", {}, ir::IRType::Void(), "");
        if (else_block) {
            lc.builder.MakeBranch(else_block);
        } else if (finally_block) {
            lc.builder.MakeBranch(finally_block);
        } else {
            lc.builder.MakeBranch(exit_block);
        }
    }

    // --- Landing pad block: dispatch to appropriate handler ---
    lc.SetInsertBlock(landing_pad);
    lc.terminated = false;
    
    // Get the current exception object and type
    auto exc_obj = lc.builder.MakeCall("__py_get_exception", 
                                       {}, 
                                       ir::IRType::Pointer(ir::IRType::I64(true)), 
                                       lc.NextTemp("exc"));
    auto exc_type = lc.builder.MakeCall("__py_get_exception_type", 
                                        {}, 
                                        ir::IRType::I64(true), 
                                        lc.NextTemp("exctype"));

    // Chain of type checks for each handler
    ir::BasicBlock *current_check_block = landing_pad;
    for (size_t i = 0; i < try_stmt->handlers.size(); ++i) {
        const auto &handler = try_stmt->handlers[i];
        
        ir::BasicBlock *next_check = (i + 1 < try_stmt->handlers.size()) 
                                      ? lc.fn->CreateBlock("except.check." + std::to_string(i + 1))
                                      : unwind_block;
        
        if (i > 0) {
            lc.SetInsertBlock(current_check_block);
        }
        
        if (handler.type) {
            // Typed handler: check if exception matches this type
            auto handler_type = EvalExpr(handler.type, lc);
            if (!handler_type.IsValid()) return false;
            
            auto type_match = lc.builder.MakeCall("__py_exception_isinstance",
                                                  {exc_obj->name, handler_type.value},
                                                  ir::IRType::I1(),
                                                  lc.NextTemp("typematch"));
            lc.builder.MakeCondBranch(type_match->name, handler_blocks[i], next_check);
        } else {
            // Bare except: catches all exceptions
            lc.builder.MakeBranch(handler_blocks[i]);
        }
        
        current_check_block = next_check;
    }

    // --- Unwind block: re-raise if no handler matched ---
    lc.SetInsertBlock(unwind_block);
    lc.terminated = false;
    
    // Pop frame and re-raise the exception
    lc.builder.MakeCall("__py_pop_exception_frame", {}, ir::IRType::Void(), "");
    if (finally_block) {
        // Must run finally before re-raising
        // Store that we need to re-raise after finally
        lc.builder.MakeCall("__py_set_reraise_flag", {}, ir::IRType::Void(), "");
        lc.builder.MakeBranch(finally_block);
    } else {
        lc.builder.MakeCall("__py_reraise", {}, ir::IRType::Void(), "");
        lc.builder.MakeUnreachable();
        lc.terminated = true;
    }

    // --- Exception handlers ---
    for (size_t i = 0; i < try_stmt->handlers.size(); ++i) {
        lc.SetInsertBlock(handler_blocks[i]);
        lc.terminated = false;
        
        const auto &handler = try_stmt->handlers[i];
        
        // Clear the exception (it's been handled)
        lc.builder.MakeCall("__py_clear_exception", {}, ir::IRType::Void(), "");
        lc.builder.MakeCall("__py_pop_exception_frame", {}, ir::IRType::Void(), "");
        
        // Bind exception to variable if named
        if (!handler.name.empty()) {
            lc.env[handler.name] = {exc_obj->name, exc_obj->type, "", false};
        }

        bool handler_term = false;
        for (auto &s : handler.body) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) {
                handler_term = true;
                break;
            }
        }

        if (!handler_term) {
            if (finally_block) {
                lc.builder.MakeBranch(finally_block);
            } else {
                lc.builder.MakeBranch(exit_block);
            }
        }
    }

    // --- Else block: executed if no exception occurred ---
    if (else_block) {
        lc.SetInsertBlock(else_block);
        lc.terminated = false;
        for (auto &s : try_stmt->orelse) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) break;
        }
        if (!lc.terminated) {
            if (finally_block) {
                lc.builder.MakeBranch(finally_block);
            } else {
                lc.builder.MakeBranch(exit_block);
            }
        }
    }

    // --- Finally block: always executed ---
    if (finally_block) {
        lc.SetInsertBlock(finally_block);
        lc.terminated = false;
        for (auto &s : try_stmt->finalbody) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) break;
        }
        if (!lc.terminated) {
            // Check if we need to re-raise after finally
            auto needs_reraise = lc.builder.MakeCall("__py_check_reraise_flag", 
                                                     {}, 
                                                     ir::IRType::I1(), 
                                                     lc.NextTemp("reraise"));
            
            auto *reraise_block = lc.fn->CreateBlock("finally.reraise");
            lc.builder.MakeCondBranch(needs_reraise->name, reraise_block, exit_block);
            
            lc.SetInsertBlock(reraise_block);
            lc.builder.MakeCall("__py_clear_reraise_flag", {}, ir::IRType::Void(), "");
            lc.builder.MakeCall("__py_reraise", {}, ir::IRType::Void(), "");
            lc.builder.MakeUnreachable();
        }
    }

    lc.SetInsertBlock(exit_block);
    lc.terminated = false;
    return true;
}

bool LowerMatch(const std::shared_ptr<MatchStatement> &match_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;

    auto subject = EvalExpr(match_stmt->subject, lc);
    if (!subject.IsValid()) return false;

    auto *exit_block = lc.fn->CreateBlock("match.end");
    
    // Create blocks for each case
    std::vector<ir::BasicBlock *> case_blocks;
    for (size_t i = 0; i < match_stmt->cases.size(); ++i) {
        case_blocks.push_back(lc.fn->CreateBlock("case." + std::to_string(i)));
    }
    case_blocks.push_back(exit_block);  // Default fallthrough

    // Generate pattern matching
    for (size_t i = 0; i < match_stmt->cases.size(); ++i) {
        const auto &mc = match_stmt->cases[i];
        
        // Check pattern match
        auto pattern_match = lc.builder.MakeCall("__py_match_pattern", 
                                                 {subject.value}, 
                                                 ir::IRType::I1(), 
                                                 lc.NextTemp("match"));
        
        // Check guard if present
        if (mc.guard) {
            auto guard_result = EvalExpr(mc.guard, lc);
            if (guard_result.IsValid()) {
                auto combined = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kAnd,
                                                      pattern_match->name, guard_result.value,
                                                      lc.NextTemp("guard"));
                combined->type = ir::IRType::I1();
                lc.builder.MakeCondBranch(combined->name, case_blocks[i], case_blocks[i + 1]);
            } else {
                lc.builder.MakeCondBranch(pattern_match->name, case_blocks[i], case_blocks[i + 1]);
            }
        } else {
            lc.builder.MakeCondBranch(pattern_match->name, case_blocks[i], case_blocks[i + 1]);
        }

        // Case body
        lc.SetInsertBlock(case_blocks[i]);
        lc.terminated = false;

        bool case_term = false;
        for (auto &s : mc.body) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) {
                case_term = true;
                break;
            }
        }
        if (!case_term) {
            lc.builder.MakeBranch(exit_block);
        }
    }

    lc.SetInsertBlock(exit_block);
    lc.terminated = false;
    return true;
}

bool LowerRaise(const std::shared_ptr<RaiseStatement> &raise, LoweringContext &lc) {
    if (lc.terminated) return true;

    std::vector<std::string> args;
    if (raise->value) {
        auto exc = EvalExpr(raise->value, lc);
        if (!exc.IsValid()) return false;
        args.push_back(exc.value);
    }
    if (raise->from_expr) {
        auto from = EvalExpr(raise->from_expr, lc);
        if (!from.IsValid()) return false;
        args.push_back(from.value);
    }

    lc.builder.MakeCall("__py_raise", args, ir::IRType::Void(), "");
    lc.builder.MakeUnreachable();
    lc.terminated = true;
    return true;
}

bool LowerAssert(const std::shared_ptr<AssertStatement> &assert_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;

    auto cond = EvalExpr(assert_stmt->test, lc);
    if (!cond.IsValid()) return false;

    auto *fail_block = lc.fn->CreateBlock("assert.fail");
    auto *pass_block = lc.fn->CreateBlock("assert.pass");

    lc.builder.MakeCondBranch(cond.value, pass_block, fail_block);

    lc.SetInsertBlock(fail_block);
    std::vector<std::string> args;
    if (assert_stmt->msg) {
        auto msg = EvalExpr(assert_stmt->msg, lc);
        if (msg.IsValid()) {
            args.push_back(msg.value);
        }
    }
    lc.builder.MakeCall("__py_assert_fail", args, ir::IRType::Void(), "");
    lc.builder.MakeUnreachable();

    lc.SetInsertBlock(pass_block);
    lc.terminated = false;
    return true;
}

bool LowerBreak(LoweringContext &lc) {
    if (lc.terminated) return true;
    if (lc.loop_stack.empty()) {
        lc.diags.Report(core::SourceLoc{}, "'break' outside loop");
        return false;
    }
    lc.builder.MakeBranch(lc.loop_stack.top().break_target);
    lc.terminated = true;
    return true;
}

bool LowerContinue(LoweringContext &lc) {
    if (lc.terminated) return true;
    if (lc.loop_stack.empty()) {
        lc.diags.Report(core::SourceLoc{}, "'continue' outside loop");
        return false;
    }
    lc.builder.MakeBranch(lc.loop_stack.top().continue_target);
    lc.terminated = true;
    return true;
}

bool LowerPass(LoweringContext &lc) {
    // Pass is a no-op
    (void)lc;
    return true;
}

// ----------------------------------------------------------------------------
// Built-in module function table for static linking.
// Maps module name to a set of known function signatures.
// ----------------------------------------------------------------------------
struct BuiltinFunctionInfo {
    ir::IRType return_type;
    std::vector<ir::IRType> param_types;
    std::string runtime_name;  // RT function to call
};

std::unordered_map<std::string, std::unordered_map<std::string, BuiltinFunctionInfo>> 
GetBuiltinModuleFunctions() {
    return {
        {"math", {
            {"sin", {ir::IRType::F64(), {ir::IRType::F64()}, "__py_math_sin"}},
            {"cos", {ir::IRType::F64(), {ir::IRType::F64()}, "__py_math_cos"}},
            {"sqrt", {ir::IRType::F64(), {ir::IRType::F64()}, "__py_math_sqrt"}},
            {"floor", {ir::IRType::I64(true), {ir::IRType::F64()}, "__py_math_floor"}},
            {"ceil", {ir::IRType::I64(true), {ir::IRType::F64()}, "__py_math_ceil"}},
            {"pow", {ir::IRType::F64(), {ir::IRType::F64(), ir::IRType::F64()}, "__py_math_pow"}},
        }},
        {"os", {
            {"getcwd", {ir::IRType::Pointer(ir::IRType::I8()), {}, "__py_os_getcwd"}},
        }},
        {"sys", {
            {"exit", {ir::IRType::Void(), {ir::IRType::I64(true)}, "__py_sys_exit"}},
        }},
        {"json", {
            {"dumps", {ir::IRType::Pointer(ir::IRType::I8()), {ir::IRType::I64(true)}, "__py_json_dumps"}},
            {"loads", {ir::IRType::I64(true), {ir::IRType::Pointer(ir::IRType::I8())}, "__py_json_loads"}},
        }},
    };
}

// Check if module/function is built-in and can be statically linked
bool IsBuiltinFunction(const std::string &module, const std::string &func) {
    auto modules = GetBuiltinModuleFunctions();
    if (auto it = modules.find(module); it != modules.end()) {
        return it->second.count(func) > 0;
    }
    return false;
}

// Get runtime function name for built-in
std::string GetBuiltinRuntimeName(const std::string &module, const std::string &func) {
    auto modules = GetBuiltinModuleFunctions();
    if (auto it = modules.find(module); it != modules.end()) {
        if (auto it2 = it->second.find(func); it2 != it->second.end()) {
            return it2->second.runtime_name;
        }
    }
    // Fallback: generate dynamic lookup name
    return "__py_" + module + "_" + func;
}

bool LowerImport(const std::shared_ptr<ImportStatement> &import_stmt, LoweringContext &lc) {
    // Generate runtime import calls and create global module handles
    auto builtin_modules = GetBuiltinModuleFunctions();
    
    if (import_stmt->is_from) {
        // from module import names
        std::string modname = import_stmt->module;
        bool is_builtin = builtin_modules.count(modname) > 0;
        
        for (const auto &alias : import_stmt->names) {
            std::string export_name = alias.name;
            std::string bind_name = alias.alias.empty() ? alias.name : alias.alias;
            
            if (is_builtin && IsBuiltinFunction(modname, export_name)) {
                // Static linking: create global reference to built-in function
                std::string rt_name = GetBuiltinRuntimeName(modname, export_name);
                auto &funcs = builtin_modules[modname];
                auto &info = funcs[export_name];
                
                // Create a global function pointer
                auto global = lc.ir_ctx.CreateGlobal(
                    "@__mod_" + modname + "_" + export_name,
                    ir::IRType::Pointer(ir::IRType::I8()),
                    true,
                    rt_name);
                
                lc.env[bind_name] = {global->name, global->type, "", false};
            } else {
                // Dynamic import: generate runtime call
                // Create string constant for module and name
                auto mod_str = lc.ir_ctx.CreateGlobal(
                    ".str.mod." + modname,
                    ir::IRType::Pointer(ir::IRType::I8()),
                    true,
                    "\"" + modname + "\"");
                    
                auto name_str = lc.ir_ctx.CreateGlobal(
                    ".str.name." + export_name,
                    ir::IRType::Pointer(ir::IRType::I8()),
                    true,
                    "\"" + export_name + "\"");
                
                // Call __py_import_from(module_name, symbol_name) -> PyObject*
                auto result = lc.builder.MakeCall(
                    "__py_import_from",
                    {mod_str->name, name_str->name},
                    ir::IRType::I64(true),
                    lc.NextTemp("import"));
                    
                lc.env[bind_name] = {result->name, result->type, "", false};
            }
        }
    } else {
        // import module [as alias]
        for (const auto &alias : import_stmt->names) {
            std::string modname = alias.name;
            std::string bind_name = alias.alias.empty() ? alias.name : alias.alias;
            
            bool is_builtin = builtin_modules.count(modname) > 0;
            
            // Create global module handle
            std::string global_name = "@__module_" + modname;
            
            if (is_builtin) {
                // Built-in module: create a module descriptor global
                auto global = lc.ir_ctx.CreateGlobal(
                    global_name,
                    ir::IRType::I64(true),
                    true,
                    "/* builtin:" + modname + " */");
                    
                lc.env[bind_name] = {global->name, global->type, "", false};
            } else {
                // Dynamic module: generate runtime import
                auto mod_str = lc.ir_ctx.CreateGlobal(
                    ".str.mod." + modname,
                    ir::IRType::Pointer(ir::IRType::I8()),
                    true,
                    "\"" + modname + "\"");
                
                // Call __py_import(module_name) -> PyModuleHandle
                auto result = lc.builder.MakeCall(
                    "__py_import",
                    {mod_str->name},
                    ir::IRType::I64(true),
                    lc.NextTemp("module"));
                
                // Store in global for later access
                auto global = lc.ir_ctx.CreateGlobal(
                    global_name,
                    ir::IRType::I64(true),
                    false,
                    "0");
                    
                // Store the imported module handle
                lc.builder.MakeStore(result->name, global->name);
                
                lc.env[bind_name] = {global->name, global->type, "", false};
            }
        }
    }
    return true;
}

bool LowerGlobal(const std::shared_ptr<GlobalStatement> &global_stmt, LoweringContext &lc) {
    // Mark names as global (affects name lookup)
    for (const auto &name : global_stmt->names) {
        // Create or reference global variable
        auto global = lc.ir_ctx.CreateGlobal(name, ir::IRType::I64(true), false, "0");
        lc.env[name] = {global->name, global->type, "", true};
    }
    return true;
}

bool LowerNonlocal(const std::shared_ptr<NonlocalStatement> &nonlocal_stmt, LoweringContext &lc) {
    // Nonlocal is handled at semantic analysis; here it's a no-op
    (void)nonlocal_stmt;
    (void)lc;
    return true;
}

// ----------------------------------------------------------------------------
// Main statement lowering dispatcher.
// ----------------------------------------------------------------------------
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
    if (!stmt || lc.terminated) return true;

    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
        return LowerReturn(ret, lc);
    }
    if (auto assign = std::dynamic_pointer_cast<Assignment>(stmt)) {
        return LowerAssign(assign, lc);
    }
    if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
        return LowerIf(if_stmt, lc);
    }
    if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
        return LowerWhile(while_stmt, lc);
    }
    if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
        return LowerFor(for_stmt, lc);
    }
    if (auto with_stmt = std::dynamic_pointer_cast<WithStatement>(stmt)) {
        return LowerWith(with_stmt, lc);
    }
    if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
        return LowerTry(try_stmt, lc);
    }
    if (auto match_stmt = std::dynamic_pointer_cast<MatchStatement>(stmt)) {
        return LowerMatch(match_stmt, lc);
    }
    if (auto raise_stmt = std::dynamic_pointer_cast<RaiseStatement>(stmt)) {
        return LowerRaise(raise_stmt, lc);
    }
    if (auto assert_stmt = std::dynamic_pointer_cast<AssertStatement>(stmt)) {
        return LowerAssert(assert_stmt, lc);
    }
    if (std::dynamic_pointer_cast<BreakStatement>(stmt)) {
        return LowerBreak(lc);
    }
    if (std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
        return LowerContinue(lc);
    }
    if (std::dynamic_pointer_cast<PassStatement>(stmt)) {
        return LowerPass(lc);
    }
    if (auto import_stmt = std::dynamic_pointer_cast<ImportStatement>(stmt)) {
        return LowerImport(import_stmt, lc);
    }
    if (auto global_stmt = std::dynamic_pointer_cast<GlobalStatement>(stmt)) {
        return LowerGlobal(global_stmt, lc);
    }
    if (auto nonlocal_stmt = std::dynamic_pointer_cast<NonlocalStatement>(stmt)) {
        return LowerNonlocal(nonlocal_stmt, lc);
    }
    if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        (void)EvalExpr(expr_stmt->expr, lc);
        return true;
    }
    // Nested function/class definitions that appear inside a block scope
    // are lowered in-place as local declarations.  Forward declarations are
    // provided so the functions can be referenced before their definition.
    if (auto fn_def = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
        // Lower the nested function as a standalone IR function.
        // This makes it callable from subsequent statements in the same scope.
        return LowerFunction(*fn_def, lc);
    }
    if (auto cls_def = std::dynamic_pointer_cast<ClassDef>(stmt)) {
        // Lower the nested class declaration so its methods are available.
        return LowerClass(*cls_def, lc);
    }

    lc.diags.Report(stmt->loc, "Unsupported statement type in lowering");
    return false;
}

// ----------------------------------------------------------------------------
// Function lowering.
// Handles parameters with default values using PHI nodes for proper SSA.
// ----------------------------------------------------------------------------
bool LowerFunction(const FunctionDef &fn, LoweringContext &lc) {
    // Determine return type from type hints
    ir::IRType ret_ty = ir::IRType::I64(true);
    if (fn.return_annotation) {
        if (auto id = std::dynamic_pointer_cast<Identifier>(fn.return_annotation)) {
            ret_ty = ToIRType(id->name);
        }
    }

    // Build parameter list
    std::vector<std::pair<std::string, ir::IRType>> params;
    params.reserve(fn.params.size());
    for (const auto &arg : fn.params) {
        ir::IRType param_ty = ir::IRType::I64(true);
        if (arg.annotation) {
            if (auto id = std::dynamic_pointer_cast<Identifier>(arg.annotation)) {
                param_ty = ToIRType(id->name);
            }
        }
        params.push_back({arg.name, param_ty});
    }

    // Handle async functions
    bool was_async = lc.in_async_function;
    lc.in_async_function = fn.is_async;

    // Mangle function name for methods
    std::string func_name = fn.name;
    if (!lc.current_class.empty()) {
        func_name = lc.current_class + "." + fn.name;
    }

    lc.fn = lc.ir_ctx.CreateFunction(func_name, ret_ty, params);
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    lc.SetInsertBlock(entry);

    lc.env.clear();
    for (const auto &p : params) {
        lc.env[p.first] = {p.first, p.second, "", false};
    }

    // Handle default arguments with PHI nodes for proper SSA
    // For each parameter with default: check if arg was provided, use default if not
    // Python's sentinel is typically a special "not provided" marker
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const auto &arg = fn.params[i];
        if (arg.default_value) {
            // Check if this argument was provided (compare with sentinel)
            auto sentinel_check = lc.builder.MakeCall(
                "__py_arg_provided",
                {arg.name, std::to_string(i)},
                ir::IRType::I1(),
                lc.NextTemp("arg_provided"));
            
            auto *use_arg_block = lc.fn->CreateBlock("default.use_arg." + arg.name);
            auto *use_default_block = lc.fn->CreateBlock("default.use_default." + arg.name);
            auto *merge_block = lc.fn->CreateBlock("default.merge." + arg.name);
            
            auto *check_block = lc.builder.GetInsertPoint().get();
            lc.builder.MakeCondBranch(sentinel_check->name, use_arg_block, use_default_block);
            
            // Branch 1: Use the provided argument value
            lc.SetInsertBlock(use_arg_block);
            std::string provided_val = arg.name;
            lc.builder.MakeBranch(merge_block);
            auto *arg_end_block = lc.builder.GetInsertPoint().get();
            
            // Branch 2: Evaluate and use the default value
            lc.SetInsertBlock(use_default_block);
            auto default_result = EvalExpr(arg.default_value, lc);
            std::string default_val = default_result.IsValid() 
                                     ? default_result.value 
                                     : MakeLiteral(0, lc).value;
            lc.builder.MakeBranch(merge_block);
            auto *default_end_block = lc.builder.GetInsertPoint().get();
            
            // Merge with PHI node
            lc.SetInsertBlock(merge_block);
            
            ir::IRType param_type = params[i].second;
            std::vector<std::pair<ir::BasicBlock*, std::string>> phi_incomings = {
                {arg_end_block, provided_val},
                {default_end_block, default_val}
            };
            
            auto phi = lc.builder.MakePhi(param_type, phi_incomings, lc.NextTemp("param"));
            
            // Update environment with PHI result
            lc.env[arg.name] = {phi->name, param_type, "", false};
        }
    }

    lc.terminated = false;

    // Lower function body
    for (const auto &stmt : fn.body) {
        if (!LowerStmt(stmt, lc)) {
            lc.in_async_function = was_async;
            return false;
        }
        if (lc.terminated) break;
    }

    // Add implicit return if needed
    if (!lc.terminated) {
        if (ret_ty.kind == ir::IRTypeKind::kVoid) {
            lc.builder.MakeReturn("");
        } else {
            auto zero = MakeLiteral(0, lc);
            lc.builder.MakeReturn(zero.value);
        }
    }

    lc.in_async_function = was_async;
    return true;
}

// ----------------------------------------------------------------------------
// Class lowering.
// ----------------------------------------------------------------------------
bool LowerClass(const ClassDef &cls, LoweringContext &lc) {
    // Register class info
    ClassInfo info;
    info.name = cls.name;

    // Save current class context
    std::string saved_class = lc.current_class;
    lc.current_class = cls.name;

    // Process base classes
    for (const auto &base : cls.bases) {
        if (auto id = std::dynamic_pointer_cast<Identifier>(base)) {
            // Inherit from base class
            lc.builder.MakeCall("__py_class_inherit", {}, ir::IRType::Void(), "");
        }
    }

    // Create class initialization function
    std::string init_fn_name = cls.name + ".__init__";
    
    // Lower methods and collect field information
    for (const auto &stmt : cls.body) {
        if (auto method = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
            info.methods.push_back(method->name);
            LowerFunction(*method, lc);
        } else if (auto assign = std::dynamic_pointer_cast<Assignment>(stmt)) {
            // Class-level assignment (class variable or annotation)
            for (const auto &target : assign->targets) {
                if (auto id = std::dynamic_pointer_cast<Identifier>(target)) {
                    info.fields.push_back(id->name);
                }
            }
        }
    }

    // Store class info
    lc.classes[cls.name] = info;
    lc.current_class = saved_class;

    return true;
}

// ----------------------------------------------------------------------------
// Decorator handling.
// ----------------------------------------------------------------------------
bool ApplyDecorators(const std::vector<std::shared_ptr<Expression>> &decorators,
                     const std::string &func_name, LoweringContext &lc) {
    for (auto it = decorators.rbegin(); it != decorators.rend(); ++it) {
        const auto &dec = *it;
        if (auto id = std::dynamic_pointer_cast<Identifier>(dec)) {
            // Apply decorator: func = decorator(func)
            lc.builder.MakeCall("__py_apply_decorator_" + id->name, {func_name},
                               ir::IRType::I64(true), func_name);
        } else if (auto call = std::dynamic_pointer_cast<CallExpression>(dec)) {
            // Decorator with arguments: @decorator(args)(func)
            if (auto callee_id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
                std::vector<std::string> args;
                for (const auto &arg : call->args) {
                    auto ev = EvalExpr(arg.value, lc);
                    if (ev.IsValid()) args.push_back(ev.value);
                }
                args.push_back(func_name);
                lc.builder.MakeCall("__py_apply_decorator_" + callee_id->name, args,
                                   ir::IRType::I64(true), func_name);
            }
        }
    }
    return true;
}

} // namespace

// ----------------------------------------------------------------------------
// Public API: Lower Python module to IR.
// ----------------------------------------------------------------------------
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
    LoweringContext lc(ctx, diags);

    // First pass: collect class and function declarations
    for (const auto &stmt : module.body) {
        if (auto cls = std::dynamic_pointer_cast<ClassDef>(stmt)) {
            ClassInfo info;
            info.name = cls->name;
            lc.classes[cls->name] = info;
        }
    }

    // Second pass: lower all top-level definitions
    for (const auto &stmt : module.body) {
        if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
            if (!LowerFunction(*fn, lc)) {
                diags.Report(fn->loc, "Failed to lower function: " + fn->name);
            }
            // Apply decorators
            if (!fn->decorators.empty()) {
                ApplyDecorators(fn->decorators, fn->name, lc);
            }
        } else if (auto cls = std::dynamic_pointer_cast<ClassDef>(stmt)) {
            if (!LowerClass(*cls, lc)) {
                diags.Report(cls->loc, "Failed to lower class: " + cls->name);
            }
            // Apply class decorators
            if (!cls->decorators.empty()) {
                ApplyDecorators(cls->decorators, cls->name, lc);
            }
        } else if (auto assign = std::dynamic_pointer_cast<Assignment>(stmt)) {
            // Module-level assignment (global variable)
            if (assign->value) {
                // Need a module init function context
                if (!lc.fn) {
                    lc.fn = ctx.CreateFunction("__module_init__", ir::IRType::Void(), {});
                    auto *entry = lc.fn->CreateBlock("entry");
                    lc.fn->entry = entry;
                    lc.SetInsertBlock(entry);
                    lc.terminated = false;
                }
                LowerAssign(assign, lc);
            }
        } else if (auto import_stmt = std::dynamic_pointer_cast<ImportStatement>(stmt)) {
            // Module-level import
            if (!lc.fn) {
                lc.fn = ctx.CreateFunction("__module_init__", ir::IRType::Void(), {});
                auto *entry = lc.fn->CreateBlock("entry");
                lc.fn->entry = entry;
                lc.SetInsertBlock(entry);
                lc.terminated = false;
            }
            LowerImport(import_stmt, lc);
        }
        // Other top-level statements are skipped
    }

    // Finalize module init function if created
    if (lc.fn && lc.fn->name == "__module_init__" && !lc.terminated) {
        lc.builder.MakeReturn("");
    }
}

} // namespace polyglot::python
