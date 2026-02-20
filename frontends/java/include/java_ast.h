#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::java {

// ============================================================================
// Base AST Nodes
// ============================================================================

struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
};

struct Statement : AstNode {};
struct Expression : AstNode {};
struct TypeNode : AstNode {};

// ============================================================================
// Type Nodes
// ============================================================================

struct SimpleType : TypeNode {
    std::string name;
};

struct ArrayType : TypeNode {
    std::shared_ptr<TypeNode> element_type;
    int dimensions{1};
};

struct GenericType : TypeNode {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> type_args;
};

struct WildcardType : TypeNode {
    enum class BoundKind { kNone, kExtends, kSuper };
    BoundKind bound_kind{BoundKind::kNone};
    std::shared_ptr<TypeNode> bound;
};

// ============================================================================
// Expression Nodes
// ============================================================================

struct Identifier : Expression {
    std::string name;
};

struct Literal : Expression {
    std::string value;
};

struct UnaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> operand;
    bool postfix{false};
};

struct BinaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

struct CallExpression : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<std::shared_ptr<Expression>> args;
};

struct MemberExpression : Expression {
    std::shared_ptr<Expression> object;
    std::string member;
};

struct NewExpression : Expression {
    std::shared_ptr<TypeNode> type;
    std::vector<std::shared_ptr<Expression>> args;
};

struct CastExpression : Expression {
    std::shared_ptr<TypeNode> target_type;
    std::shared_ptr<Expression> expr;
};

struct ArrayAccessExpression : Expression {
    std::shared_ptr<Expression> array;
    std::shared_ptr<Expression> index;
};

struct LambdaExpression : Expression {
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type; // nullptr for inferred
    };
    std::vector<Param> params;
    std::shared_ptr<Statement> body;
};

struct TernaryExpression : Expression {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> then_expr;
    std::shared_ptr<Expression> else_expr;
};

struct InstanceofExpression : Expression {
    std::shared_ptr<Expression> expr;
    std::shared_ptr<TypeNode> type;
    std::string pattern_var; // Java 16+ pattern matching: if (obj instanceof String s)
};

struct MethodReferenceExpression : Expression {
    std::shared_ptr<Expression> object; // or type
    std::string method_name;            // "new" for constructor references
};

struct SwitchExpression : Expression {
    std::shared_ptr<Expression> selector;
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

struct Annotation {
    std::string name;
    std::vector<std::shared_ptr<Expression>> args;
};

struct Parameter {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::vector<Annotation> annotations;
    bool is_varargs{false};
    bool is_final{false};
};

struct BlockStatement : Statement {
    std::vector<std::shared_ptr<Statement>> statements;
};

struct VarDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type; // nullptr for 'var' (Java 10+)
    std::shared_ptr<Expression> init;
    bool is_final{false};
    std::vector<Annotation> annotations;
};

struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

struct IfStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> then_body;
    std::shared_ptr<Statement> else_body;
};

struct WhileStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> body;
};

struct ForStatement : Statement {
    std::shared_ptr<Statement> init;
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> update;
    std::shared_ptr<Statement> body;
};

struct ForEachStatement : Statement {
    std::string var_name;
    std::shared_ptr<TypeNode> var_type;
    std::shared_ptr<Expression> iterable;
    std::shared_ptr<Statement> body;
    bool is_final{false};
};

struct SwitchStatement : Statement {
    std::shared_ptr<Expression> selector;
    struct Case {
        std::vector<std::shared_ptr<Expression>> labels; // empty for default
        std::vector<std::shared_ptr<Statement>> body;
        bool is_arrow{false}; // Java 14+ arrow case
    };
    std::vector<Case> cases;
};

struct TryStatement : Statement {
    std::shared_ptr<Statement> body;
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

struct ThrowStatement : Statement {
    std::shared_ptr<Expression> expr;
};

struct BreakStatement : Statement {
    std::string label; // empty if no label
};

struct ContinueStatement : Statement {
    std::string label;
};

struct AssertStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> message;
};

struct YieldStatement : Statement {
    std::shared_ptr<Expression> value; // Java 14+ switch expressions
};

struct SynchronizedStatement : Statement {
    std::shared_ptr<Expression> monitor;
    std::shared_ptr<Statement> body;
};

// ============================================================================
// Top-Level Declarations
// ============================================================================

struct TypeParameter {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> bounds; // extends bounds
};

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

struct ConstructorDecl : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<Annotation> annotations;
    std::string access;
    std::vector<std::shared_ptr<TypeNode>> throws_types;
};

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

struct EnumDecl : Statement {
    std::string name;
    std::string access;
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
struct RecordDecl : Statement {
    std::string name;
    std::string access;
    std::vector<Parameter> components;
    std::vector<TypeParameter> type_params;
    std::vector<std::shared_ptr<TypeNode>> interfaces;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Annotation> annotations;
};

struct ImportDecl : Statement {
    std::string path; // e.g., "java.util.List" or "java.util.*"
    bool is_static{false};
};

struct PackageDecl : Statement {
    std::string name;
    std::vector<Annotation> annotations;
};

// ============================================================================
// Module (compilation unit)
// ============================================================================

struct Module {
    std::string filename;
    std::shared_ptr<PackageDecl> package_decl;
    std::vector<std::shared_ptr<ImportDecl>> imports;
    std::vector<std::shared_ptr<Statement>> declarations;
};

} // namespace polyglot::java
