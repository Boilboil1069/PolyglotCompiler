#pragma once

#include <unordered_map>
#include <variant>
#include <string>
#include <vector>
#include <memory>

namespace polyglot::cpp {

// Forward declarations
struct Expression;
struct Statement;
struct BinaryExpression;
struct UnaryExpression;
struct ConditionalExpression;
struct Literal;
struct Identifier;
struct CallExpression;
struct MemberExpression;
struct IndexExpression;
struct StaticCastExpression;
struct InitializerListExpression;
struct VarDecl;
struct ReturnStatement;
struct IfStatement;
struct ExprStatement;

// Compile-time value representation
class ConstexprValue {
public:
    enum class Type {
        kInt,
        kFloat,
        kBool,
        kPointer,
        kNull,
        kUndefined
    };
    
    ConstexprValue() : type_(Type::kUndefined) {}
    
    explicit ConstexprValue(long long val) 
        : type_(Type::kInt), int_value_(val) {}
    
    explicit ConstexprValue(double val) 
        : type_(Type::kFloat), float_value_(val) {}
    
    explicit ConstexprValue(bool val) 
        : type_(Type::kBool), bool_value_(val) {}
    
    static ConstexprValue Null() {
        ConstexprValue v;
        v.type_ = Type::kNull;
        return v;
    }
    
    Type GetType() const { return type_; }
    bool IsUndefined() const { return type_ == Type::kUndefined; }
    
    long long AsInt() const { return int_value_; }
    double AsFloat() const { return float_value_; }
    bool AsBool() const { return bool_value_; }
    
    std::string ToString() const;
    
private:
    Type type_;
    union {
        long long int_value_;
        double float_value_;
        bool bool_value_;
    };
};

// Constexpr evaluator - evaluates constant expressions at compile time
class ConstexprEvaluator {
public:
    ConstexprEvaluator();
    
    // Evaluate an expression at compile time
    ConstexprValue Evaluate(Expression* expr);
    
    // Check if a function can be evaluated at compile time
    bool IsConstexprFunction(VarDecl* func) const;
    
    // Evaluate a constexpr function call
    ConstexprValue EvaluateCall(VarDecl* func, 
                                const std::vector<ConstexprValue>& args);
    
    // Set variable value in current scope
    void SetVariable(const std::string& name, const ConstexprValue& value);
    
    // Get variable value from current scope
    ConstexprValue GetVariable(const std::string& name) const;
    
    // Enter new scope (for function calls)
    void PushScope();
    
    // Exit current scope
    void PopScope();
    
private:
    ConstexprValue EvaluateBinaryOp(BinaryExpression* expr);
    ConstexprValue EvaluateUnaryOp(UnaryExpression* expr);
    ConstexprValue EvaluateConditional(ConditionalExpression* expr);
    ConstexprValue EvaluateLiteral(Literal* expr);
    ConstexprValue EvaluateVariable(Identifier* expr);
    ConstexprValue EvaluateCallExpr(CallExpression* expr);
    ConstexprValue EvaluateMemberAccess(MemberExpression* expr);
    ConstexprValue EvaluateArraySubscript(IndexExpression* expr);
    ConstexprValue EvaluateCast(StaticCastExpression* expr);
    ConstexprValue EvaluateCall(CallExpression* expr);
    ConstexprValue EvaluateInitializerList(InitializerListExpression* expr);
    
    // Evaluate statement (for constexpr functions)
    bool EvaluateStmt(Statement* stmt, ConstexprValue& return_value);
    bool EvaluateIfStmt(IfStatement* stmt, ConstexprValue& return_value);
    bool EvaluateReturnStmt(ReturnStatement* stmt, ConstexprValue& return_value);
    bool EvaluateDeclStmt(VarDecl* stmt);
    
    // Apply binary operator
    ConstexprValue ApplyBinaryOp(const std::string& op, 
                                 const ConstexprValue& lhs,
                                 const ConstexprValue& rhs);
    
    // Apply unary operator
    ConstexprValue ApplyUnaryOp(const std::string& op, const ConstexprValue& val);
    
    // Scope management
    struct Scope {
        std::unordered_map<std::string, ConstexprValue> variables;
    };
    
    std::vector<Scope> scopes_;
    
    // Cache of constexpr function results
    std::unordered_map<std::string, ConstexprValue> function_cache_;
    
    // Recursion depth limit
    static constexpr int kMaxRecursionDepth = 512;
    int recursion_depth_ = 0;
};

// Constexpr checker - validates that expressions/functions can be constexpr
class ConstexprChecker {
public:
    ConstexprChecker();
    
    // Check if expression is a constant expression
    bool IsConstantExpression(Expression* expr) const;
    
    // Check if function can be constexpr
    bool CanBeConstexpr(VarDecl* func) const;
    
    // Check if statement is valid in constexpr context
    bool IsConstexprStatement(Statement* stmt) const;
    
private:
    bool IsConstexprExpr(Expression* expr) const;
    bool CheckCallExpr(CallExpression* expr) const;
};

}  // namespace polyglot::cpp
