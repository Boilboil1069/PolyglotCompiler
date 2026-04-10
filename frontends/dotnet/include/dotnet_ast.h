/**
 * @file     dotnet_ast.h
 * @brief    .NET/C# language frontend
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::dotnet {

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
    int rank{1}; // multi-dimensional arrays
};

/** @brief GenericType data structure. */
struct GenericType : TypeNode {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> type_args;
};

/** @brief NullableType data structure. */
struct NullableType : TypeNode {
    std::shared_ptr<TypeNode> inner;
};

/** @brief TupleType data structure. */
struct TupleType : TypeNode {
    /** @brief Element data structure. */
    struct Element {
        std::shared_ptr<TypeNode> type;
        std::string name; // optional tuple element name
    };
    std::vector<Element> elements;
};

/** @brief FunctionPointerType data structure. */
struct FunctionPointerType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> param_types;
    std::shared_ptr<TypeNode> return_type;
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
    bool null_conditional{false}; // ?. operator
};

/** @brief NewExpression data structure. */
struct NewExpression : Expression {
    std::shared_ptr<TypeNode> type;
    std::vector<std::shared_ptr<Expression>> args;
    std::vector<std::shared_ptr<Expression>> initializer; // object/collection initializer
};

/** @brief CastExpression data structure. */
struct CastExpression : Expression {
    std::shared_ptr<TypeNode> target_type;
    std::shared_ptr<Expression> expr;
};

/** @brief AsExpression data structure. */
struct AsExpression : Expression {
    std::shared_ptr<Expression> expr;
    std::shared_ptr<TypeNode> type;
};

/** @brief IsExpression data structure. */
struct IsExpression : Expression {
    std::shared_ptr<Expression> expr;
    std::shared_ptr<TypeNode> type;
    std::string pattern_var; // pattern matching: if (obj is string s)
};

/** @brief IndexExpression data structure. */
struct IndexExpression : Expression {
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
    bool null_conditional{false}; // ?[] operator
};

/** @brief LambdaExpression data structure. */
struct LambdaExpression : Expression {
    /** @brief Param data structure. */
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;
        bool is_ref{false};
        bool is_out{false};
    };
    std::vector<Param> params;
    std::shared_ptr<Statement> body;   // block body
    std::shared_ptr<Expression> expr;  // expression body
    bool is_async{false};
};

/** @brief TernaryExpression data structure. */
struct TernaryExpression : Expression {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> then_expr;
    std::shared_ptr<Expression> else_expr;
};

/** @brief NullCoalescingExpression data structure. */
struct NullCoalescingExpression : Expression {
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

/** @brief AwaitExpression data structure. */
struct AwaitExpression : Expression {
    std::shared_ptr<Expression> operand;
};

/** @brief ThrowExpression data structure. */
struct ThrowExpression : Expression {
    std::shared_ptr<Expression> operand;
};

/** @brief InterpolatedString data structure. */
struct InterpolatedString : Expression {
    /** @brief Part data structure. */
    struct Part {
        bool is_literal{true};
        std::string text;
        std::shared_ptr<Expression> expr;
        std::string format; // optional format specifier
    };
    std::vector<Part> parts;
};

/** @brief SwitchExpression data structure. */
struct SwitchExpression : Expression {
    std::shared_ptr<Expression> governing;
    /** @brief Arm data structure. */
    struct Arm {
        std::shared_ptr<Expression> pattern;
        std::shared_ptr<Expression> guard; // when clause
        std::shared_ptr<Expression> value;
    };
    std::vector<Arm> arms;
};

/** @brief RangeExpression data structure. */
struct RangeExpression : Expression {
    std::shared_ptr<Expression> start; // nullable for ^
    std::shared_ptr<Expression> end;   // nullable
};

/** @brief TupleExpression data structure. */
struct TupleExpression : Expression {
    /** @brief Element data structure. */
    struct Element {
        std::string name;
        std::shared_ptr<Expression> value;
    };
    std::vector<Element> elements;
};

/** @brief TypeofExpression data structure. */
struct TypeofExpression : Expression {
    std::shared_ptr<TypeNode> type;
};

/** @brief NameofExpression data structure. */
struct NameofExpression : Expression {
    std::string name;
};

/** @brief DefaultExpression data structure. */
struct DefaultExpression : Expression {
    std::shared_ptr<TypeNode> type; // nullable for default literal
};

// ============================================================================
// Statement Nodes
// ============================================================================

/** @brief Attribute data structure. */
struct Attribute {
    std::string name;
    std::vector<std::shared_ptr<Expression>> args;
};

/** @brief Parameter data structure. */
struct Parameter {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> default_value;
    std::vector<Attribute> attributes;
    bool is_ref{false};
    bool is_out{false};
    bool is_in{false};
    bool is_params{false};
    bool is_this{false}; // extension method this parameter
};

/** @brief BlockStatement data structure. */
struct BlockStatement : Statement {
    std::vector<std::shared_ptr<Statement>> statements;
};

/** @brief VarDecl data structure. */
struct VarDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type; // nullptr for 'var'
    std::shared_ptr<Expression> init;
    bool is_const{false};
    bool is_readonly{false};
    std::vector<Attribute> attributes;
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

/** @brief DoWhileStatement data structure. */
struct DoWhileStatement : Statement {
    std::shared_ptr<Statement> body;
    std::shared_ptr<Expression> condition;
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
};

/** @brief SwitchStatement data structure. */
struct SwitchStatement : Statement {
    std::shared_ptr<Expression> governing;
    /** @brief Section data structure. */
    struct Section {
        std::vector<std::shared_ptr<Expression>> labels;
        std::vector<std::shared_ptr<Statement>> body;
        bool is_default{false};
    };
    std::vector<Section> sections;
};

/** @brief TryStatement data structure. */
struct TryStatement : Statement {
    std::shared_ptr<Statement> body;
    /** @brief CatchClause data structure. */
    struct CatchClause {
        std::string var_name;
        std::shared_ptr<TypeNode> exception_type;
        std::shared_ptr<Expression> filter; // when clause (C# exception filters)
        std::shared_ptr<Statement> body;
    };
    std::vector<CatchClause> catches;
    std::shared_ptr<Statement> finally_body;
};

/** @brief ThrowStatement data structure. */
struct ThrowStatement : Statement {
    std::shared_ptr<Expression> expr; // nullptr for rethrow
};

/** @brief UsingStatement data structure. */
struct UsingStatement : Statement {
    std::shared_ptr<VarDecl> declaration;
    std::shared_ptr<Expression> expression;
    std::shared_ptr<Statement> body;
    bool is_await{false}; // C# 8.0+
};

/** @brief LockStatement data structure. */
struct LockStatement : Statement {
    std::shared_ptr<Expression> expr;
    std::shared_ptr<Statement> body;
};

/** @brief YieldStatement data structure. */
struct YieldStatement : Statement {
    std::shared_ptr<Expression> value; // nullptr for yield break
    bool is_break{false};
};

/** @brief BreakStatement data structure. */
struct BreakStatement : Statement {};
/** @brief ContinueStatement data structure. */
struct ContinueStatement : Statement {};

/** @brief FixedStatement data structure. */
struct FixedStatement : Statement {
    std::shared_ptr<VarDecl> declaration;
    std::shared_ptr<Statement> body;
};

/** @brief CheckedStatement data structure. */
struct CheckedStatement : Statement {
    std::shared_ptr<Statement> body;
    bool is_unchecked{false};
};

// ============================================================================
// Top-Level Declarations
// ============================================================================

/** @brief TypeParameter data structure. */
struct TypeParameter {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> constraints;
    bool has_new_constraint{false};
    bool has_class_constraint{false};
    bool has_struct_constraint{false};
    bool has_unmanaged_constraint{false};
    bool has_notnull_constraint{false};
    bool has_default_constraint{false}; // .NET 7+
};

/** @brief MethodDecl data structure. */
struct MethodDecl : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::shared_ptr<TypeNode> return_type;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<Attribute> attributes;
    std::vector<TypeParameter> type_params;
    std::string access;
    bool is_static{false};
    bool is_abstract{false};
    bool is_virtual{false};
    bool is_override{false};
    bool is_sealed{false};
    bool is_async{false};
    bool is_partial{false};
    bool is_extern{false};
    bool is_new{false}; // new modifier (hides base member)
    std::shared_ptr<Expression> expression_body; // => expr;
};

/** @brief PropertyDecl data structure. */
struct PropertyDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::string access;
    bool is_static{false};
    bool is_virtual{false};
    bool is_override{false};
    bool is_abstract{false};
    bool has_getter{false};
    bool has_setter{false};
    bool is_init_only{false}; // C# 9.0 init accessor
    bool is_required{false};  // C# 11 required modifier
    std::shared_ptr<Expression> init;
    std::shared_ptr<Expression> expression_body;
    std::vector<Attribute> attributes;
};

/** @brief EventDecl data structure. */
struct EventDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::string access;
    bool is_static{false};
    std::vector<Attribute> attributes;
};

/** @brief IndexerDecl data structure. */
struct IndexerDecl : Statement {
    std::shared_ptr<TypeNode> type;
    std::vector<Parameter> params;
    std::string access;
    bool has_getter{false};
    bool has_setter{false};
    std::vector<Attribute> attributes;
};

/** @brief FieldDecl data structure. */
struct FieldDecl : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> init;
    std::vector<Attribute> attributes;
    std::string access;
    bool is_static{false};
    bool is_readonly{false};
    bool is_const{false};
    bool is_volatile{false};
    bool is_required{false}; // C# 11 required
};

/** @brief ConstructorDecl data structure. */
struct ConstructorDecl : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<Attribute> attributes;
    std::string access;
    std::string initializer_kind; // "base" or "this"
    std::vector<std::shared_ptr<Expression>> initializer_args;
    bool is_static{false};
};

/** @brief DestructorDecl data structure. */
struct DestructorDecl : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief ClassDecl data structure. */
struct ClassDecl : Statement {
    std::string name;
    std::string access;
    bool is_abstract{false};
    bool is_sealed{false};
    bool is_static{false};
    bool is_partial{false};
    bool is_record{false};  // record class (C# 9.0+)
    bool is_file_scoped{false}; // C# 11 file-scoped types
    std::vector<TypeParameter> type_params;
    std::shared_ptr<TypeNode> base_type;
    std::vector<std::shared_ptr<TypeNode>> interfaces;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Attribute> attributes;
    std::vector<Parameter> primary_ctor_params; // C# 12 primary constructors
};

/** @brief StructDecl data structure. */
struct StructDecl : Statement {
    std::string name;
    std::string access;
    bool is_readonly{false};
    bool is_ref{false};
    bool is_record{false}; // record struct (C# 10)
    bool is_partial{false};
    std::vector<TypeParameter> type_params;
    std::vector<std::shared_ptr<TypeNode>> interfaces;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Attribute> attributes;
    std::vector<Parameter> primary_ctor_params;
};

/** @brief InterfaceDecl data structure. */
struct InterfaceDecl : Statement {
    std::string name;
    std::string access;
    bool is_partial{false};
    std::vector<TypeParameter> type_params;
    std::vector<std::shared_ptr<TypeNode>> extends_types;
    std::vector<std::shared_ptr<Statement>> members;
    std::vector<Attribute> attributes;
};

/** @brief EnumDecl data structure. */
struct EnumDecl : Statement {
    std::string name;
    std::string access;
    std::shared_ptr<TypeNode> underlying_type; // : int, byte, etc.
    /** @brief EnumMember data structure. */
    struct EnumMember {
        std::string name;
        std::shared_ptr<Expression> value;
        std::vector<Attribute> attributes;
    };
    std::vector<EnumMember> members;
    std::vector<Attribute> attributes;
};

/** @brief DelegateDecl data structure. */
struct DelegateDecl : Statement {
    std::string name;
    std::string access;
    std::shared_ptr<TypeNode> return_type;
    std::vector<Parameter> params;
    std::vector<TypeParameter> type_params;
    std::vector<Attribute> attributes;
};

/** @brief NamespaceDecl data structure. */
struct NamespaceDecl : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> members;
    bool is_file_scoped{false}; // C# 10 file-scoped namespace
};

/** @brief UsingDirective data structure. */
struct UsingDirective : Statement {
    std::string ns;            // namespace name
    std::string alias;         // alias = name
    bool is_static{false};
    bool is_global{false};     // C# 10 global usings
};

// ============================================================================
// Module (compilation unit)
// ============================================================================

/** @brief Module data structure. */
struct Module {
    std::string filename;
    std::vector<std::shared_ptr<UsingDirective>> usings;
    std::vector<std::shared_ptr<Statement>> declarations;
    // Top-level statements (C# 9.0+)
    std::vector<std::shared_ptr<Statement>> top_level_statements;
};

} // namespace polyglot::dotnet
