#include <catch2/catch_test_macros.hpp>
#include "frontends/cpp/include/cpp_constexpr.h"

using namespace polyglot::cpp;

TEST_CASE("Constexpr - Basic Value", "[constexpr]") {
    ConstexprValue val1(42);
    REQUIRE(val1.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(val1.AsInt() == 42);
    
    ConstexprValue val2(3.14);
    REQUIRE(val2.GetType() == ConstexprValue::Type::kFloat);
    REQUIRE(val2.AsFloat() == 3.14);
    
    ConstexprValue val3(true);
    REQUIRE(val3.GetType() == ConstexprValue::Type::kBool);
    REQUIRE(val3.AsBool() == true);
}

TEST_CASE("Constexpr - Binary Operations", "[constexpr]") {
    ConstexprEvaluator eval;
    
    ConstexprValue lhs(10);
    ConstexprValue rhs(5);
    
    ConstexprValue result = eval.ApplyBinaryOp("+", lhs, rhs);
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 15);
    
    result = eval.ApplyBinaryOp("-", lhs, rhs);
    REQUIRE(result.AsInt() == 5);
    
    result = eval.ApplyBinaryOp("*", lhs, rhs);
    REQUIRE(result.AsInt() == 50);
}

TEST_CASE("Constexpr - Unary Operations", "[constexpr]") {
    ConstexprEvaluator eval;
    
    ConstexprValue val(42);
    ConstexprValue result = eval.ApplyUnaryOp("-", val);
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == -42);
}

TEST_CASE("Constexpr - Variable Scope", "[constexpr]") {
    ConstexprEvaluator eval;
    
    // Set a variable
    eval.SetVariable("x", ConstexprValue(42));
    
    // Retrieve it
    ConstexprValue val = eval.GetVariable("x");
    
    REQUIRE(val.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(val.AsInt() == 42);
}

TEST_CASE("Constexpr - Nested Scopes", "[constexpr]") {
    ConstexprEvaluator eval;
    
    eval.SetVariable("x", ConstexprValue(10));
    
    eval.PushScope();
    eval.SetVariable("y", ConstexprValue(20));
    
    REQUIRE(eval.GetVariable("x").AsInt() == 10);
    REQUIRE(eval.GetVariable("y").AsInt() == 20);
    
    eval.PopScope();
    
    REQUIRE(eval.GetVariable("x").AsInt() == 10);
    REQUIRE(eval.GetVariable("y").IsUndefined() == true);
}

TEST_CASE("Constexpr - Integer Arithmetic", "[constexpr]") {
    ConstexprEvaluator eval;
    
    // Test: 5 + 3
    auto expr = std::make_unique<BinaryExpr>();
    expr->op = BinaryExpr::Op::kAdd;
    expr->lhs = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->lhs.get())->kind = LiteralExpr::Kind::kInteger;
    static_cast<LiteralExpr*>(expr->lhs.get())->int_value = 5;
    expr->rhs = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->rhs.get())->kind = LiteralExpr::Kind::kInteger;
    static_cast<LiteralExpr*>(expr->rhs.get())->int_value = 3;
    
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 8);
}

TEST_CASE("Constexpr - Multiplication", "[constexpr]") {
    ConstexprEvaluator eval;
    
    // Test: 4 * 7
    auto expr = std::make_unique<BinaryExpr>();
    expr->op = BinaryExpr::Op::kMul;
    expr->lhs = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->lhs.get())->kind = LiteralExpr::Kind::kInteger;
    static_cast<LiteralExpr*>(expr->lhs.get())->int_value = 4;
    expr->rhs = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->rhs.get())->kind = LiteralExpr::Kind::kInteger;
    static_cast<LiteralExpr*>(expr->rhs.get())->int_value = 7;
    
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 28);
}

TEST_CASE("Constexpr - Conditional Expression", "[constexpr]") {
    ConstexprEvaluator eval;
    
    // Test: true ? 10 : 20
    auto expr = std::make_unique<ConditionalExpr>();
    
    expr->condition = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->condition.get())->kind = LiteralExpr::Kind::kBool;
    static_cast<LiteralExpr*>(expr->condition.get())->bool_value = true;
    
    expr->true_expr = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->true_expr.get())->kind = LiteralExpr::Kind::kInteger;
    static_cast<LiteralExpr*>(expr->true_expr.get())->int_value = 10;
    
    expr->false_expr = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->false_expr.get())->kind = LiteralExpr::Kind::kInteger;
    static_cast<LiteralExpr*>(expr->false_expr.get())->int_value = 20;
    
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 10);
}

TEST_CASE("Constexpr - Float Operations", "[constexpr]") {
    ConstexprEvaluator eval;
    
    // Test: 3.5 + 2.5
    auto expr = std::make_unique<BinaryExpr>();
    expr->op = BinaryExpr::Op::kAdd;
    expr->lhs = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->lhs.get())->kind = LiteralExpr::Kind::kFloat;
    static_cast<LiteralExpr*>(expr->lhs.get())->float_value = 3.5;
    expr->rhs = std::make_unique<LiteralExpr>();
    static_cast<LiteralExpr*>(expr->rhs.get())->kind = LiteralExpr::Kind::kFloat;
    static_cast<LiteralExpr*>(expr->rhs.get())->float_value = 2.5;
    
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kFloat);
    REQUIRE(result.AsFloat() == 6.0);
}

TEST_CASE("Constexpr - Variable Scope", "[constexpr]") {
    ConstexprEvaluator eval;
    
    // Set a variable
    eval.SetVariable("x", ConstexprValue(42));
    
    // Retrieve it
    ConstexprValue val = eval.GetVariable("x");
    
    REQUIRE(val.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(val.AsInt() == 42);
}

TEST_CASE("Constexpr Checker - Constant Expression", "[constexpr]") {
    ConstexprChecker checker;
    
    // Literal is always constant
    auto lit = std::make_unique<LiteralExpr>();
    lit->kind = LiteralExpr::Kind::kInteger;
    lit->int_value = 100;
    
    REQUIRE(checker.IsConstantExpression(lit.get()) == true);
}
