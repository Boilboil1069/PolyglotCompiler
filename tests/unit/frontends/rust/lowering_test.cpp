// Rust Lowering Tests
// Tests for the Rust AST to IR lowering including:
// - Complete expression lowering (all expression types)
// - Complete statement lowering (all statement types)
// - Full parameter type handling
// - Control flow lowering (if, while, for, loop, match)

#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/diagnostics.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_lowering.h"
#include "middle/include/ir/ir_context.h"

using polyglot::frontends::Diagnostics;
using polyglot::ir::IRContext;
using namespace polyglot::rust;

namespace {

// Helper to create a simple identifier
std::shared_ptr<Identifier> MakeId(const std::string &name) {
    auto id = std::make_shared<Identifier>();
    id->name = name;
    return id;
}

// Helper to create a literal
std::shared_ptr<Literal> MakeLit(const std::string &value) {
    auto lit = std::make_shared<Literal>();
    lit->value = value;
    return lit;
}

// Helper to create a type path
std::shared_ptr<TypePath> MakeTypePath(const std::string &type_name) {
    auto tp = std::make_shared<TypePath>();
    tp->segments.push_back(type_name);
    return tp;
}

// Helper to create identifier pattern
std::shared_ptr<IdentifierPattern> MakeIdPattern(const std::string &name) {
    auto pat = std::make_shared<IdentifierPattern>();
    pat->name = name;
    return pat;
}

// Helper to create let statement
std::shared_ptr<LetStatement> MakeLet(const std::string &name, 
                                       std::shared_ptr<Expression> init,
                                       std::shared_ptr<TypeNode> type = nullptr) {
    auto let = std::make_shared<LetStatement>();
    let->pattern = MakeIdPattern(name);
    let->init = init;
    let->type_annotation = type;
    return let;
}

// Helper to create binary expression
std::shared_ptr<BinaryExpression> MakeBinary(const std::string &op,
                                              std::shared_ptr<Expression> left,
                                              std::shared_ptr<Expression> right) {
    auto bin = std::make_shared<BinaryExpression>();
    bin->op = op;
    bin->left = left;
    bin->right = right;
    return bin;
}

// Helper to create unary expression
std::shared_ptr<UnaryExpression> MakeUnary(const std::string &op, 
                                            std::shared_ptr<Expression> operand) {
    auto un = std::make_shared<UnaryExpression>();
    un->op = op;
    un->operand = operand;
    return un;
}

// Helper to create return statement
std::shared_ptr<ReturnStatement> MakeReturn(std::shared_ptr<Expression> value = nullptr) {
    auto ret = std::make_shared<ReturnStatement>();
    ret->value = value;
    return ret;
}

// Helper to create function item
std::shared_ptr<FunctionItem> MakeFunction(const std::string &name,
                                            std::vector<FunctionItem::Param> params,
                                            std::shared_ptr<TypeNode> ret_type,
                                            std::vector<std::shared_ptr<Statement>> body) {
    auto fn = std::make_shared<FunctionItem>();
    fn->name = name;
    fn->params = std::move(params);
    fn->return_type = ret_type;
    fn->body = std::move(body);
    fn->has_body = true;
    return fn;
}

} // namespace

// ============ Test 1: Basic Expression Lowering ============
TEST_CASE("Rust Lowering - Basic Expressions", "[rust][lowering]") {
    SECTION("Integer literal lowering") {
        Module mod;
        
        // fn test() -> i64 { return 42; }
        auto fn = MakeFunction("test", {}, MakeTypePath("i64"), {MakeReturn(MakeLit("42"))});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        REQUIRE(ctx.Functions()[0]->name == "test");
    }
    
    SECTION("Boolean literal lowering") {
        Module mod;
        
        // fn test() -> bool { return true; }
        auto fn = MakeFunction("test", {}, MakeTypePath("bool"), {MakeReturn(MakeLit("true"))});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
    
    SECTION("Float literal lowering") {
        Module mod;
        
        // fn test() -> f64 { return 3.14; }
        auto fn = MakeFunction("test", {}, MakeTypePath("f64"), {MakeReturn(MakeLit("3.14"))});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
}

// ============ Test 2: Binary Expression Lowering ============
TEST_CASE("Rust Lowering - Binary Expressions", "[rust][lowering]") {
    SECTION("Arithmetic operators") {
        Module mod;
        
        // fn add(a: i64, b: i64) -> i64 { return a + b; }
        FunctionItem::Param p1, p2;
        p1.name = "a";
        p1.type = MakeTypePath("i64");
        p2.name = "b";
        p2.type = MakeTypePath("i64");
        
        auto add = MakeBinary("+", MakeId("a"), MakeId("b"));
        auto fn = MakeFunction("add", {p1, p2}, MakeTypePath("i64"), {MakeReturn(add)});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        REQUIRE(diags.All().empty());
    }
    
    SECTION("Comparison operators") {
        Module mod;
        
        // fn less(a: i64, b: i64) -> bool { return a < b; }
        FunctionItem::Param p1, p2;
        p1.name = "a";
        p1.type = MakeTypePath("i64");
        p2.name = "b";
        p2.type = MakeTypePath("i64");
        
        auto cmp = MakeBinary("<", MakeId("a"), MakeId("b"));
        auto fn = MakeFunction("less", {p1, p2}, MakeTypePath("bool"), {MakeReturn(cmp)});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
    
    SECTION("Bitwise operators") {
        Module mod;
        
        // fn band(a: i64, b: i64) -> i64 { return a & b; }
        FunctionItem::Param p1, p2;
        p1.name = "a";
        p1.type = MakeTypePath("i64");
        p2.name = "b";
        p2.type = MakeTypePath("i64");
        
        auto band = MakeBinary("&", MakeId("a"), MakeId("b"));
        auto fn = MakeFunction("band", {p1, p2}, MakeTypePath("i64"), {MakeReturn(band)});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
}

// ============ Test 3: Unary Expression Lowering ============
TEST_CASE("Rust Lowering - Unary Expressions", "[rust][lowering]") {
    SECTION("Negation operator") {
        Module mod;
        
        // fn neg(x: i64) -> i64 { return -x; }
        FunctionItem::Param p;
        p.name = "x";
        p.type = MakeTypePath("i64");
        
        auto neg = MakeUnary("-", MakeId("x"));
        auto fn = MakeFunction("neg", {p}, MakeTypePath("i64"), {MakeReturn(neg)});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
    
    SECTION("Logical not operator") {
        Module mod;
        
        // fn not_fn(x: bool) -> bool { return !x; }
        FunctionItem::Param p;
        p.name = "x";
        p.type = MakeTypePath("bool");
        
        auto not_op = MakeUnary("!", MakeId("x"));
        auto fn = MakeFunction("not_fn", {p}, MakeTypePath("bool"), {MakeReturn(not_op)});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
}

// ============ Test 4: Control Flow Lowering ============
TEST_CASE("Rust Lowering - Control Flow", "[rust][lowering]") {
    SECTION("If expression lowering") {
        Module mod;
        
        // fn max(a: i64, b: i64) -> i64 {
        //     if a > b { return a; } else { return b; }
        // }
        FunctionItem::Param p1, p2;
        p1.name = "a";
        p1.type = MakeTypePath("i64");
        p2.name = "b";
        p2.type = MakeTypePath("i64");
        
        auto cond = MakeBinary(">", MakeId("a"), MakeId("b"));
        
        auto if_expr = std::make_shared<IfExpression>();
        if_expr->condition = cond;
        if_expr->then_body.push_back(MakeReturn(MakeId("a")));
        if_expr->else_body.push_back(MakeReturn(MakeId("b")));
        
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = if_expr;
        
        auto fn = MakeFunction("max", {p1, p2}, MakeTypePath("i64"), {expr_stmt});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        // Should have multiple basic blocks for if/then/else
        REQUIRE(ctx.Functions()[0]->blocks.size() >= 3);
    }
    
    SECTION("Loop statement lowering") {
        Module mod;
        
        // fn infinite() {
        //     loop { break; }
        // }
        auto brk = std::make_shared<BreakStatement>();
        
        auto loop = std::make_shared<LoopStatement>();
        loop->body.push_back(brk);
        
        auto fn = MakeFunction("infinite", {}, nullptr, {loop});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        // Should have loop body and exit blocks
        REQUIRE(ctx.Functions()[0]->blocks.size() >= 2);
    }
    
    SECTION("While expression lowering") {
        Module mod;
        
        // fn countdown(n: i64) {
        //     while n > 0 { let x = 1; }
        // }
        FunctionItem::Param p;
        p.name = "n";
        p.type = MakeTypePath("i64");
        
        auto cond = MakeBinary(">", MakeId("n"), MakeLit("0"));
        auto let_x = MakeLet("x", MakeLit("1"));
        
        auto wh = std::make_shared<WhileExpression>();
        wh->condition = cond;
        wh->body.push_back(let_x);
        
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = wh;
        
        auto fn = MakeFunction("countdown", {p}, nullptr, {expr_stmt});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        // Should have cond, body, and exit blocks
        REQUIRE(ctx.Functions()[0]->blocks.size() >= 3);
    }
}

// ============ Test 5: Let Statement Lowering ============
TEST_CASE("Rust Lowering - Let Statements", "[rust][lowering]") {
    SECTION("Simple let binding") {
        Module mod;
        
        // fn test() -> i64 {
        //     let x = 42;
        //     return x;
        // }
        auto let_x = MakeLet("x", MakeLit("42"), MakeTypePath("i64"));
        auto ret = MakeReturn(MakeId("x"));
        
        auto fn = MakeFunction("test", {}, MakeTypePath("i64"), {let_x, ret});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        REQUIRE(diags.All().empty());
    }
    
    SECTION("Let with expression") {
        Module mod;
        
        // fn test(a: i64, b: i64) -> i64 {
        //     let sum = a + b;
        //     return sum;
        // }
        FunctionItem::Param p1, p2;
        p1.name = "a";
        p1.type = MakeTypePath("i64");
        p2.name = "b";
        p2.type = MakeTypePath("i64");
        
        auto sum_expr = MakeBinary("+", MakeId("a"), MakeId("b"));
        auto let_sum = MakeLet("sum", sum_expr);
        auto ret = MakeReturn(MakeId("sum"));
        
        auto fn = MakeFunction("test", {p1, p2}, MakeTypePath("i64"), {let_sum, ret});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        REQUIRE(diags.All().empty());
    }
}

// ============ Test 6: Type Handling ============
TEST_CASE("Rust Lowering - Type Handling", "[rust][lowering]") {
    SECTION("All integer types") {
        Module mod;
        
        std::vector<std::string> int_types = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"};
        
        for (size_t i = 0; i < int_types.size(); ++i) {
            FunctionItem::Param p;
            p.name = "x";
            p.type = MakeTypePath(int_types[i]);
            
            auto fn = MakeFunction("fn_" + int_types[i], {p}, MakeTypePath(int_types[i]), 
                                   {MakeReturn(MakeId("x"))});
            mod.items.push_back(fn);
        }
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == int_types.size());
    }
    
    SECTION("Float types") {
        Module mod;
        
        // fn f32_fn(x: f32) -> f32 { return x; }
        FunctionItem::Param p1;
        p1.name = "x";
        p1.type = MakeTypePath("f32");
        auto fn1 = MakeFunction("f32_fn", {p1}, MakeTypePath("f32"), {MakeReturn(MakeId("x"))});
        mod.items.push_back(fn1);
        
        // fn f64_fn(x: f64) -> f64 { return x; }
        FunctionItem::Param p2;
        p2.name = "x";
        p2.type = MakeTypePath("f64");
        auto fn2 = MakeFunction("f64_fn", {p2}, MakeTypePath("f64"), {MakeReturn(MakeId("x"))});
        mod.items.push_back(fn2);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 2);
    }
    
    SECTION("Bool type") {
        Module mod;
        
        // fn bool_fn(x: bool) -> bool { return x; }
        FunctionItem::Param p;
        p.name = "x";
        p.type = MakeTypePath("bool");
        auto fn = MakeFunction("bool_fn", {p}, MakeTypePath("bool"), {MakeReturn(MakeId("x"))});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
}

// ============ Test 7: Match Expression Lowering ============
TEST_CASE("Rust Lowering - Match Expression", "[rust][lowering]") {
    SECTION("Simple match with literals") {
        Module mod;
        
        // fn test(x: i64) -> i64 {
        //     match x {
        //         1 => 10,
        //         _ => 0
        //     }
        // }
        FunctionItem::Param p;
        p.name = "x";
        p.type = MakeTypePath("i64");
        
        auto match = std::make_shared<MatchExpression>();
        match->scrutinee = MakeId("x");
        
        // Arm 1: 1 => 10
        auto arm1 = std::make_shared<MatchArm>();
        auto lit_pat = std::make_shared<LiteralPattern>();
        lit_pat->value = "1";
        arm1->pattern = lit_pat;
        arm1->body = MakeLit("10");
        
        // Arm 2: _ => 0
        auto arm2 = std::make_shared<MatchArm>();
        arm2->pattern = std::make_shared<WildcardPattern>();
        arm2->body = MakeLit("0");
        
        match->arms.push_back(arm1);
        match->arms.push_back(arm2);
        
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = match;
        
        auto fn = MakeFunction("test", {p}, MakeTypePath("i64"), {expr_stmt, MakeReturn(MakeLit("0"))});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        // Should have blocks for each arm
        REQUIRE(ctx.Functions()[0]->blocks.size() >= 3);
    }
}

// ============ Test 8: Call Expression Lowering ============
TEST_CASE("Rust Lowering - Call Expression", "[rust][lowering]") {
    SECTION("Simple function call") {
        Module mod;
        
        // fn callee(x: i64) -> i64 { return x; }
        // fn caller() -> i64 { return callee(42); }
        
        FunctionItem::Param p;
        p.name = "x";
        p.type = MakeTypePath("i64");
        auto fn1 = MakeFunction("callee", {p}, MakeTypePath("i64"), {MakeReturn(MakeId("x"))});
        mod.items.push_back(fn1);
        
        auto path = std::make_shared<PathExpression>();
        path->segments.push_back("callee");
        
        auto call = std::make_shared<CallExpression>();
        call->callee = path;
        call->args.push_back(MakeLit("42"));
        
        auto fn2 = MakeFunction("caller", {}, MakeTypePath("i64"), {MakeReturn(call)});
        mod.items.push_back(fn2);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 2);
    }
}

// ============ Test 9: Break and Continue ============
TEST_CASE("Rust Lowering - Break and Continue", "[rust][lowering]") {
    SECTION("Break in loop") {
        Module mod;
        
        // fn test() {
        //     loop {
        //         break;
        //     }
        // }
        auto brk = std::make_shared<BreakStatement>();
        
        auto loop = std::make_shared<LoopStatement>();
        loop->body.push_back(brk);
        
        auto fn = MakeFunction("test", {}, nullptr, {loop});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        REQUIRE(diags.All().empty());
    }
    
    SECTION("Continue in loop") {
        Module mod;
        
        // fn test() {
        //     loop {
        //         continue;
        //     }
        // }
        auto cont = std::make_shared<ContinueStatement>();
        
        auto loop = std::make_shared<LoopStatement>();
        loop->body.push_back(cont);
        
        auto fn = MakeFunction("test", {}, nullptr, {loop});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
        REQUIRE(diags.All().empty());
    }
}

// ============ Test 10: Assignment Expression ============
TEST_CASE("Rust Lowering - Assignment Expression", "[rust][lowering]") {
    SECTION("Simple assignment") {
        Module mod;
        
        // fn test() -> i64 {
        //     let mut x = 0;
        //     x = 42;
        //     return x;
        // }
        auto let_x = MakeLet("x", MakeLit("0"));
        
        auto assign = std::make_shared<AssignmentExpression>();
        assign->op = "=";
        assign->left = MakeId("x");
        assign->right = MakeLit("42");
        
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = assign;
        
        auto fn = MakeFunction("test", {}, MakeTypePath("i64"), {let_x, expr_stmt, MakeReturn(MakeId("x"))});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
    
    SECTION("Compound assignment") {
        Module mod;
        
        // fn test() -> i64 {
        //     let mut x = 10;
        //     x += 5;
        //     return x;
        // }
        auto let_x = MakeLet("x", MakeLit("10"));
        
        auto assign = std::make_shared<AssignmentExpression>();
        assign->op = "+=";
        assign->left = MakeId("x");
        assign->right = MakeLit("5");
        
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = assign;
        
        auto fn = MakeFunction("test", {}, MakeTypePath("i64"), {let_x, expr_stmt, MakeReturn(MakeId("x"))});
        mod.items.push_back(fn);
        
        IRContext ctx;
        Diagnostics diags;
        LowerToIR(mod, ctx, diags);
        
        REQUIRE(ctx.Functions().size() == 1);
    }
}
