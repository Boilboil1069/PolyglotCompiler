#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "frontends/cpp/include/cpp_constexpr.h"

using namespace polyglot::cpp;

namespace {

std::shared_ptr<Literal> MakeIntLiteral(long long value) {
    auto lit = std::make_shared<Literal>();
    lit->value = std::to_string(value);
    return lit;
}

std::shared_ptr<Literal> MakeFloatLiteral(double value) {
    auto lit = std::make_shared<Literal>();
    std::ostringstream oss;
    oss << value;
    lit->value = oss.str();
    return lit;
}

std::shared_ptr<Literal> MakeBoolLiteral(bool value) {
    auto lit = std::make_shared<Literal>();
    lit->value = value ? "true" : "false";
    return lit;
}

std::shared_ptr<BinaryExpression> MakeBinaryExpr(
    const std::string& op,
    std::shared_ptr<Expression> lhs,
    std::shared_ptr<Expression> rhs) {
    auto expr = std::make_shared<BinaryExpression>();
    expr->op = op;
    expr->left = lhs;
    expr->right = rhs;
    return expr;
}

std::shared_ptr<UnaryExpression> MakeUnaryExpr(
    const std::string& op,
    std::shared_ptr<Expression> operand) {
    auto expr = std::make_shared<UnaryExpression>();
    expr->op = op;
    expr->operand = operand;
    return expr;
}

std::shared_ptr<ConditionalExpression> MakeConditionalExpr(
    std::shared_ptr<Expression> condition,
    std::shared_ptr<Expression> then_expr,
    std::shared_ptr<Expression> else_expr) {
    auto expr = std::make_shared<ConditionalExpression>();
    expr->condition = condition;
    expr->then_expr = then_expr;
    expr->else_expr = else_expr;
    return expr;
}

}  // namespace

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
    
    auto add_expr = MakeBinaryExpr("+", MakeIntLiteral(10), MakeIntLiteral(5));
    ConstexprValue result = eval.Evaluate(add_expr.get());
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 15);
    
    auto sub_expr = MakeBinaryExpr("-", MakeIntLiteral(10), MakeIntLiteral(5));
    result = eval.Evaluate(sub_expr.get());
    REQUIRE(result.AsInt() == 5);
    
    auto mul_expr = MakeBinaryExpr("*", MakeIntLiteral(10), MakeIntLiteral(5));
    result = eval.Evaluate(mul_expr.get());
    REQUIRE(result.AsInt() == 50);
}

TEST_CASE("Constexpr - Unary Operations", "[constexpr]") {
    ConstexprEvaluator eval;
    
    auto expr = MakeUnaryExpr("-", MakeIntLiteral(42));
    ConstexprValue result = eval.Evaluate(expr.get());
    
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
    
    auto expr = MakeBinaryExpr("+", MakeIntLiteral(5), MakeIntLiteral(3));
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 8);
}

TEST_CASE("Constexpr - Multiplication", "[constexpr]") {
    ConstexprEvaluator eval;
    
    auto expr = MakeBinaryExpr("*", MakeIntLiteral(4), MakeIntLiteral(7));
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 28);
}

TEST_CASE("Constexpr - Conditional Expression", "[constexpr]") {
    ConstexprEvaluator eval;
    
    auto expr = MakeConditionalExpr(
        MakeBoolLiteral(true),
        MakeIntLiteral(10),
        MakeIntLiteral(20));
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kInt);
    REQUIRE(result.AsInt() == 10);
}

TEST_CASE("Constexpr - Float Operations", "[constexpr]") {
    ConstexprEvaluator eval;
    
    auto expr = MakeBinaryExpr("+", MakeFloatLiteral(3.5), MakeFloatLiteral(2.5));
    ConstexprValue result = eval.Evaluate(expr.get());
    
    REQUIRE(result.GetType() == ConstexprValue::Type::kFloat);
    REQUIRE(result.AsFloat() == 6.0);
}

TEST_CASE("Constexpr Checker - Constant Expression", "[constexpr]") {
    ConstexprChecker checker;
    
    // Literal is always constant
    auto lit = MakeIntLiteral(100);
    
    REQUIRE(checker.IsConstantExpression(lit.get()) == true);
}
