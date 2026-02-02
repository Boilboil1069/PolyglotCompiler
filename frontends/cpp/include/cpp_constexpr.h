#pragma once

#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <string>
#include <vector>
#include <memory>
#include <type_traits>

#include "frontends/cpp/include/cpp_ast.h"

namespace polyglot::cpp {

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

    template <typename T,
              typename = std::enable_if_t<std::is_arithmetic_v<std::decay_t<T>> &&
                                          !std::is_same_v<std::decay_t<T>, bool> &&
                                          !std::is_same_v<std::decay_t<T>, long long> &&
                                          !std::is_same_v<std::decay_t<T>, double>>>
    explicit ConstexprValue(T val) {
        if constexpr (std::is_integral_v<std::decay_t<T>>) {
            type_ = Type::kInt;
            int_value_ = static_cast<long long>(val);
        } else {
            type_ = Type::kFloat;
            float_value_ = static_cast<double>(val);
        }
    }
    
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
    bool IsConstexprFunction(FunctionDecl* func) const;
    
    // Evaluate a constexpr function call (legacy - for VarDecl-based functions)
    ConstexprValue EvaluateCall(VarDecl* func, 
                                const std::vector<ConstexprValue>& args);
    
    // Evaluate a constexpr function call (FunctionDecl-based)
    ConstexprValue EvaluateFunctionCall(FunctionDecl* func,
                                        const std::vector<ConstexprValue>& args);
    
    // Register a function for constexpr evaluation
    void RegisterFunction(const std::string& name, FunctionDecl* func);
    
    // Look up a registered function by name
    FunctionDecl* LookupFunction(const std::string& name) const;
    
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
    bool EvaluateWhileStmt(WhileStatement* stmt, ConstexprValue& return_value);
    bool EvaluateForStmt(ForStatement* stmt, ConstexprValue& return_value);
    bool EvaluateCompoundStmt(CompoundStatement* stmt, ConstexprValue& return_value);
    
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
    
    // Function table for constexpr evaluation
    std::unordered_map<std::string, FunctionDecl*> function_table_;
    
    // Cache of constexpr function results
    std::unordered_map<std::string, ConstexprValue> function_cache_;
    
    // Recursion depth limit
    static constexpr int kMaxRecursionDepth = 512;
    int recursion_depth_ = 0;
    
    // Iteration limit for constexpr loops
    static constexpr int kMaxIterations = 1000000;
};

// Constexpr checker - validates that expressions/functions can be constexpr
class ConstexprChecker {
public:
    ConstexprChecker();
    
    // Check if expression is a constant expression
    bool IsConstantExpression(Expression* expr) const;
    
    // Check if function can be constexpr (VarDecl-based)
    bool CanBeConstexpr(VarDecl* func) const;
    
    // Check if function can be constexpr (FunctionDecl-based)
    bool CanBeConstexpr(FunctionDecl* func) const;
    
    // Check if statement is valid in constexpr context
    bool IsConstexprStatement(Statement* stmt) const;
    
    // Register a known constexpr function
    void RegisterConstexprFunction(const std::string& name);
    
    // Check if a function is known to be constexpr
    bool IsKnownConstexprFunction(const std::string& name) const;
    
private:
    // Core expression checker implementation
    bool IsConstexprExpr(Expression* expr) const;
    
    // Call expression checker
    bool CheckCallExpr(CallExpression* expr) const;
    
    // Check if a binary expression is constexpr
    bool CheckBinaryExpr(BinaryExpression* expr) const;
    
    // Check if a unary expression is constexpr
    bool CheckUnaryExpr(UnaryExpression* expr) const;
    
    // Check if a conditional expression is constexpr
    bool CheckConditionalExpr(ConditionalExpression* expr) const;
    
    // Set of known constexpr function names
    std::unordered_set<std::string> constexpr_functions_;
};

}  // namespace polyglot::cpp
