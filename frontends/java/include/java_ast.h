/**
 * @file     java_ast.h
 * @brief    Java language frontend
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::java {

// ============================================================================
// Base AST Nodes
// ============================================================================

/** @brief AstNode data structure. */
struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
};

/** @brief Statement data structure. */
struct Statement : AstNode {};
/** @brief Expression data structure. */
struct Expression : AstNode {};
/** @brief TypeNode data structure. */
struct TypeNode : AstNode {};

// ============================================================================
// Type Nodes
// ============================================================================

/** @brief SimpleType data structure. */
struct SimpleType : TypeNode {
    std::string name;
};

/** @brief ArrayType data structure. */
struct ArrayType : TypeNode {
    std::shared_ptr<TypeNode> element_type;
    int dimensions{1};
};

/** @brief GenericType data structure. */
struct GenericType : TypeNode {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> type_args;
};

/** @brief WildcardType data structure. */
struct WildcardType : TypeNode {
    /** @brief BoundKind enumeration. */
    enum class BoundKind { kNone, kExtends, kSuper };
    BoundKind bound_kind{BoundKind::kNone};
    std::shared_ptr<TypeNode> bound;
};

// ============================================================================
// Expression Nodes
// ============================================================================

/** @brief Identifier data structure. */
struct Identifier : Expression {
    std::string name;
};

/** @brief Literal data structure. */
struct Literal : Expression {
    std::string value;
};

/** @brief UnaryExpression data structure. */
struct UnaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> operand;
    bool postfix{false};
};

/** @brief BinaryExpression data structure. */
struct BinaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

/** @brief CallExpression data structure. */
struct CallExpression : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<std::shared_ptr<Expression>> args;
};

/** @brief MemberExpression data structure. */
struct MemberExpression : Expression {
    std::shared_ptr<Expression> object;
    std::string member;
};

/** @brief NewExpression data structure. */
struct NewExpression : Expression {
    std::shared_ptr<TypeNode> type;
    std::vector<std::shared_ptr<Expression>> args;
};

/** @brief CastExpression data structure. */
struct CastExpression : Expression {
    std::shared_ptr<TypeNode> target_type;
    std::shared_ptr<Expression> expr;
};

/** @brief ArrayAccessExpression data structure. */
struct ArrayAccessExpression : Expression {
    std::shared_ptr<Expression> array;
    std::shared_ptr<Expression> index;
};

/** @brief LambdaExpression data structure. */
struct LambdaExpression : Expression {
    /** @brief Param data structure. */
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type; // nullptr for inferred
    };
    std::vector<Param> params;
    std::shared_ptr<Statement> body;
};

/** @brief TernaryExpression data structure. */
struct TernaryExpression : Expression {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> then_expr;
    std::shared_ptr<Expression> else_expr;
};

/** @brief InstanceofExpression data structure. */
struct InstanceofExpression : Expression {
    std::shared_ptr<Expression> expr;
    std::shared_ptr<TypeNode> type;
    std::string pattern_var; // Java 16+ pattern matching: if (obj instanceof String s)
};

/** @brief MethodReferenceExpression data structure. */
struct MethodReferenceExpression : Expression {
    std::shared_ptr<Expression> object; // or type
    std::string method_name;            // "new" for constructor references
};

/** @brief SwitchExpression data structure. */
struct SwitchExpression : Expression {
    std::shared_ptr<Expression> selector;
    /** @brief Case data structure. */
    struct Case {
        std::vector<std::shared_ptr<Expression>> labels; // empty for default
        std::shared_ptr<Expression> value;
        std::vector<std::shared_ptr<Statement>> body;
        bool is_arrow{false}; // Java 14+ arrow syntax
    };
    std::vector<Case> cases;
};

// ============================================================================
// Statement Nodes
// ============================================================================

/** @brief Annotation data structure. */
struct Annotation {
    std::string name;
    std::vector<std::shared_ptr<Expression>> args;
};

/** @brief Parameter data structure. */
struct Parameter {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::vector<Annotation> annotations;
    bool is_varargs{false};
    bool is_final{false};
};

/** @brief BlockStatement data structure. */
struct BlockStatement : Statement {
    std::vector<std::shared_ptr<Statement>> statements;
};

/** @brief VarDecl data structure. */
struct VarDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type; // nullptr for 'var' (Java 10+)
    std::shared_ptr<Expression> init;
    bool is_final{false};
    std::vector<Annotation> annotations;
};

/** @brief ExprStatement data structure. */
struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

/** @brief ReturnStatement data structure. */
struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

/** @brief IfStatement data structure. */
struct IfStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> then_body;
    std::shared_ptr<Statement> else_body;
};

/** @brief WhileStatement data structure. */
struct WhileStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> body;
};

/** @brief ForStatement data structure. */
struct ForStatement : Statement {
    std::shared_ptr<Statement> init;
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> update;
    std::shared_ptr<Statement> body;
};

/** @brief ForEachStatement data structure. */
struct ForEachStatement : Statement {
    std::string var_name;
    std::shared_ptr<TypeNode> var_type;
    std::shared_ptr<Expression> iterable;
    std::shared_ptr<Statement> body;
    bool is_final{false};
};

/** @brief SwitchStatement data structure. */
struct SwitchStatement : Statement {
    std::shared_ptr<Expression> selector;
    /** @brief Case data structure. */
    struct Case {
        std::vector<std::shared_ptr<Expression>> labels; // empty for default
        std::vector<std::shared_ptr<Statement>> body;
        bool is_arrow{false}; // Java 14+ arrow case
    };
    std::vector<Case> cases;
};

/** @brief TryStatement data structure. */
struct TryStatement : Statement {
    std::shared_ptr<Statement> body;
    /** @brief CatchClause data structure. */
    struct CatchClause {
        std::string var_name;
        std::vector<std::shared_ptr<TypeNode>> exception_types; // multi-catch (Java 7+)
        std::shared_ptr<Statement> body;
    };
    std::vector<CatchClause> catches;
    std::shared_ptr<Statement> finally_body;
    // Try-with-resources (Java 7+)
    std::vector<std::shared_ptr<Statement>> resources;
};

/** @brief ThrowStatement data structure. */
struct ThrowStatement : Statement {
    std::shared_ptr<Expression> expr;
};

/** @brief BreakStatement data structure. */
struct BreakStatement : Statement {
    std::string label; // empty if no label
};

/** @brief ContinueStatement data structure. */
struct ContinueStatement : Statement {
    std::string label;
};

/** @brief AssertStatement data structure. */
struct AssertStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> message;
};

/** @brief YieldStatement data structure. */
struct YieldStatement : Statement {
    std::shared_ptr<Expression> value; // Java 14+ switch expressions
};

/** @brief SynchronizedStatement data structure. */
struct SynchronizedStatement : Statement {
    std::shared_ptr<Expression> monitor;
    std::shared_ptr<Statement> body;
};

// ============================================================================
// Top-Level Declarations
// ============================================================================

/** @brief TypeParameter data structure. */
struct TypeParameter {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> bounds; // extends bounds
};

/** @brief MethodDecl data structure. */
struct MethodDecl : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::shared_ptr<TypeNode> return_type;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<Annotation> annotations;
    std::vector<TypeParameter> type_params;
    std::string access; // "public", "private", "protected", ""
    bool is_static{false};
    bool is_abstract{false};
    bool is_final{false};
    bool is_synchronized{false};
    bool is_native{false};
    bool is_default{false}; // interface default method (Java 8+)
    std::vector<std::shared_ptr<TypeNode>> throws_types;
};

/** @brief FieldDecl data structure. */
struct FieldDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> init;
    std::vector<Annotation> annotations;
    std::string access;
    bool is_static{false};
    bool is_final{false};
    bool is_volatile{false};
    bool is_transient{false};
};

/** @brief ConstructorDecl data structure. */
struct ConstructorDecl : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<Annotation> annotations;
    std::string access;
    std::vector<std::shared_ptr<TypeNode>> throws_types;
};

/** @brief ClassDecl data structure. */
struct ClassDecl : Statement {
    std::string name;
    std::string access;
    bool is_abstract{false};
    bool is_final{false};
    bool is_static{false}; // inner static class
    std::vector<TypeParameter> type_params;
    std::shared_ptr<TypeNode> superclass;
    std::vector<std::shared_ptr<TypeNode>> interfaces;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Annotation> annotations;
    // Sealed class (Java 17+)
    bool is_sealed{false};
    bool is_non_sealed{false};
    std::vector<std::string> permits; // permitted subclasses
};

/** @brief InterfaceDecl data structure. */
struct InterfaceDecl : Statement {
    std::string name;
    std::string access;
    std::vector<TypeParameter> type_params;
    std::vector<std::shared_ptr<TypeNode>> extends_types;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Annotation> annotations;
    bool is_sealed{false};
    std::vector<std::string> permits;
};

/** @brief EnumDecl data structure. */
struct EnumDecl : Statement {
    std::string name;
    std::string access;
    /** @brief EnumConstant data structure. */
    struct EnumConstant {
        std::string name;
        std::vector<std::shared_ptr<Expression>> args;
        std::vector<std::shared_ptr<Statement>> body; // anonymous class body
        std::vector<Annotation> annotations;
    };
    std::vector<EnumConstant> constants;
    std::vector<std::shared_ptr<TypeNode>> interfaces;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Annotation> annotations;
};

// Java 16+ record
/** @brief RecordDecl data structure. */
struct RecordDecl : Statement {
    std::string name;
    std::string access;
    std::vector<Parameter> components;
    std::vector<TypeParameter> type_params;
    std::vector<std::shared_ptr<TypeNode>> interfaces;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Annotation> annotations;
};

/** @brief ImportDecl data structure. */
struct ImportDecl : Statement {
    std::string path; // e.g., "java.util.List" or "java.util.*"
    bool is_static{false};
};

/** @brief PackageDecl data structure. */
struct PackageDecl : Statement {
    std::string name;
    std::vector<Annotation> annotations;
};

// ============================================================================
// Module (compilation unit)
// ============================================================================

/** @brief Module data structure. */
struct Module {
    std::string filename;
    std::shared_ptr<PackageDecl> package_decl;
    std::vector<std::shared_ptr<ImportDecl>> imports;
    std::vector<std::shared_ptr<Statement>> declarations;
};

} // namespace polyglot::java
