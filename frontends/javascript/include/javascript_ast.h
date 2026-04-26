/**
 * @file     javascript_ast.h
 * @brief    JavaScript (ES2020+) AST definitions
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::javascript {

// ============================================================================
// Base AST Nodes
// ============================================================================

/** @brief AstNode base. */
struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
};

struct Statement : AstNode {};
struct Expression : AstNode {};
struct TypeNode : AstNode {};

// ============================================================================
// Type Nodes (extracted from JSDoc / TS-style annotations in comments)
// ============================================================================

/** @brief NamedType — a single identifier type (number, string, …). */
struct NamedType : TypeNode {
    std::string name;
};

/** @brief GenericType — Array<T>, Map<K,V>, Promise<T>, … */
struct GenericType : TypeNode {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> args;
};

/** @brief UnionType — `number | string`. */
struct UnionType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> options;
};

// ============================================================================
// Expressions
// ============================================================================

struct Identifier : Expression {
    std::string name;
};

struct Literal : Expression {
    /** @brief Literal kind. */
    enum class Kind { kNumber, kString, kTemplateString, kBool, kNull, kUndefined, kRegex, kBigInt };
    Kind kind{Kind::kNumber};
    std::string value;
};

struct ArrayExpr : Expression {
    std::vector<std::shared_ptr<Expression>> elements; // nullptr = hole
};

struct ObjectExpr : Expression {
    /** @brief Property in an object literal. */
    struct Property {
        std::string key;
        std::shared_ptr<Expression> value;
        bool computed{false};
        bool shorthand{false};
        bool spread{false};
        bool is_method{false};
    };
    std::vector<Property> properties;
};

struct UnaryExpr : Expression {
    std::string op;       // !, -, +, ~, typeof, void, delete
    std::shared_ptr<Expression> operand;
    bool prefix{true};
};

struct UpdateExpr : Expression {
    std::string op;       // ++ or --
    std::shared_ptr<Expression> target;
    bool prefix{true};
};

struct BinaryExpr : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

struct LogicalExpr : Expression {
    std::string op;       // &&, ||, ??
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

struct AssignExpr : Expression {
    std::string op;       // =, +=, -=, *=, /=, %=, **=, &&=, ||=, ??=
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> value;
};

struct ConditionalExpr : Expression {
    std::shared_ptr<Expression> test;
    std::shared_ptr<Expression> then_branch;
    std::shared_ptr<Expression> else_branch;
};

struct MemberExpr : Expression {
    std::shared_ptr<Expression> object;
    std::string property;
    bool computed{false};
    bool optional{false};   // ?.prop
};

struct CallExpr : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<std::shared_ptr<Expression>> args;
    bool optional{false};   // ?.()
    bool is_new{false};
};

struct ArrowFunction : Expression {
    /** @brief Parameter (destructuring stored as full identifier text). */
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;       // JSDoc-derived
        std::shared_ptr<Expression> default_value;
        bool rest{false};
    };
    std::vector<Param> params;
    std::shared_ptr<Statement> body;          // BlockStatement or expression-body wrapper
    std::shared_ptr<TypeNode> return_type;    // JSDoc-derived
    bool is_async{false};
    bool body_is_expression{false};
};

struct FunctionExpr : Expression {
    std::string name;     // empty for anonymous
    std::vector<ArrowFunction::Param> params;
    std::shared_ptr<Statement> body;
    std::shared_ptr<TypeNode> return_type;
    bool is_async{false};
    bool is_generator{false};
};

struct TemplateLiteral : Expression {
    std::vector<std::string> quasis;                          // string parts
    std::vector<std::shared_ptr<Expression>> expressions;     // ${...} parts
};

struct SpreadExpr : Expression {
    std::shared_ptr<Expression> arg;
};

struct AwaitExpr : Expression {
    std::shared_ptr<Expression> arg;
};

struct YieldExpr : Expression {
    std::shared_ptr<Expression> arg;
    bool delegate{false};   // yield*
};

struct SequenceExpr : Expression {
    std::vector<std::shared_ptr<Expression>> exprs;
};

// ============================================================================
// Statements
// ============================================================================

struct BlockStatement : Statement {
    std::vector<std::shared_ptr<Statement>> statements;
};

struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

/** @brief VariableDeclaration covers let/const/var with one or more declarators. */
struct VariableDecl : Statement {
    std::string kind;                                         // "let", "const", "var"
    /** @brief A single declarator within a `let a = 1, b = 2;`. */
    struct Declarator {
        std::string name;
        std::shared_ptr<TypeNode> type;
        std::shared_ptr<Expression> init;
    };
    std::vector<Declarator> decls;
};

struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

struct IfStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> then_branch;
    std::shared_ptr<Statement> else_branch;
};

struct WhileStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> body;
};

struct DoWhileStatement : Statement {
    std::shared_ptr<Statement> body;
    std::shared_ptr<Expression> condition;
};

struct ForStatement : Statement {
    std::shared_ptr<Statement> init;          // VariableDecl or ExprStatement or null
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> update;
    std::shared_ptr<Statement> body;
};

struct ForInOfStatement : Statement {
    bool is_of{false};                        // false = for-in, true = for-of
    std::string var_kind;                     // "let"/"const"/"var" or empty
    std::string var_name;
    std::shared_ptr<Expression> iterable;
    std::shared_ptr<Statement> body;
};

struct SwitchStatement : Statement {
    std::shared_ptr<Expression> discriminant;
    /** @brief A single case in a switch. */
    struct Case {
        std::shared_ptr<Expression> test;     // null = default
        std::vector<std::shared_ptr<Statement>> body;
    };
    std::vector<Case> cases;
};

struct TryStatement : Statement {
    std::shared_ptr<Statement> block;
    std::string catch_var;                    // empty if no catch
    std::shared_ptr<Statement> handler;
    std::shared_ptr<Statement> finalizer;
};

struct ThrowStatement : Statement {
    std::shared_ptr<Expression> value;
};

struct BreakStatement : Statement {
    std::string label;
};

struct ContinueStatement : Statement {
    std::string label;
};

struct LabeledStatement : Statement {
    std::string label;
    std::shared_ptr<Statement> body;
};

// ============================================================================
// Declarations
// ============================================================================

/** @brief FunctionDecl mirrors FunctionExpr but is a top-level statement. */
struct FunctionDecl : Statement {
    std::string name;
    std::vector<ArrowFunction::Param> params;
    std::shared_ptr<Statement> body;
    std::shared_ptr<TypeNode> return_type;
    bool is_async{false};
    bool is_generator{false};
    bool exported{false};
    bool exported_default{false};
};

/** @brief Method definition inside a class. */
struct MethodDecl : Statement {
    std::string name;
    std::vector<ArrowFunction::Param> params;
    std::shared_ptr<Statement> body;
    std::shared_ptr<TypeNode> return_type;
    /** @brief Method kind. */
    enum class Kind { kMethod, kConstructor, kGetter, kSetter };
    Kind kind{Kind::kMethod};
    bool is_static{false};
    bool is_async{false};
    bool is_generator{false};
    bool is_private{false};   // # prefix
};

struct FieldDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> init;
    bool is_static{false};
    bool is_private{false};
};

struct ClassDecl : Statement {
    std::string name;
    std::shared_ptr<Expression> superclass;
    std::vector<std::shared_ptr<Statement>> members;
    bool exported{false};
    bool exported_default{false};
};

struct ImportDecl : Statement {
    /** @brief A single import specifier (e.g. `{a as b}`). */
    struct Specifier {
        std::string imported;     // foreign name
        std::string local;        // local binding
        bool is_default{false};
        bool is_namespace{false}; // import * as ns
    };
    std::vector<Specifier> specifiers;
    std::string source;
};

struct ExportDecl : Statement {
    std::shared_ptr<Statement> declaration;   // function/class/var
    /** @brief A re-export specifier. */
    struct Specifier {
        std::string local;
        std::string exported;
    };
    std::vector<Specifier> specifiers;
    std::string source;
    bool is_default{false};
    std::shared_ptr<Expression> default_expr;
};

// ============================================================================
// Module
// ============================================================================

/** @brief Top-level compilation unit. */
struct Module {
    std::string filename;
    std::vector<std::shared_ptr<Statement>> body;
};

}  // namespace polyglot::javascript
