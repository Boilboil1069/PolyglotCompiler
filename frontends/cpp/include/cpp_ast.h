/**
 * @file     cpp_ast.h
 * @brief    C++ language frontend
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::cpp {

    /** @brief AstNode data structure. */
    struct AstNode {
        virtual ~AstNode() = default;
        core::SourceLoc loc{};
    };
    /** @brief Statement data structure. */
    struct Statement : AstNode {};
    /** @brief Expression data structure. */
    struct Expression : AstNode {};
    struct ConditionalExpression;
    struct TemplateIdExpression;
    /** @brief TypeNode data structure. */
    struct TypeNode : AstNode {};
    /** @brief Attribute data structure. */
    struct Attribute {std::string text;};
    /** @brief SimpleType data structure. */
    struct SimpleType : TypeNode {std::string name;};
    /** @brief PointerType data structure. */
    struct PointerType : TypeNode {
        std::shared_ptr<TypeNode> pointee;
        bool is_const{false};
        bool is_volatile{false};
    };
    /** @brief ReferenceType data structure. */
    struct ReferenceType : TypeNode {
        std::shared_ptr<TypeNode> referent;
        bool is_rvalue{false};
    };
    /** @brief ArrayType data structure. */
    struct ArrayType : TypeNode {
        std::shared_ptr<TypeNode> element_type;
        std::string size_expr;
    };
    /** @brief FunctionType data structure. */
    struct FunctionType : TypeNode {
        std::shared_ptr<TypeNode> return_type;
        std::vector<std::shared_ptr<TypeNode>> params;
    };
    /** @brief QualifiedType data structure. */
    struct QualifiedType : TypeNode {
        bool is_const{false};
        bool is_volatile{false};
        std::shared_ptr<TypeNode> inner;
    };
    /** @brief Identifier data structure. */
    struct Identifier : Expression {
        std::string name;
        std::vector<std::shared_ptr<Expression>> template_args;
    };
    /** @brief Literal data structure. */
    struct Literal : Expression {std::string value;};
    /** @brief UnaryExpression data structure. */
    struct UnaryExpression : Expression {
        std::string op;
        std::shared_ptr<Expression> operand;
    };
    /** @brief BinaryExpression data structure. */
    struct BinaryExpression : Expression {
        std::string op;
        std::shared_ptr<Expression> left;
        std::shared_ptr<Expression> right;
    };
    /** @brief NewExpression data structure. */
    struct NewExpression : Expression {
        std::shared_ptr<TypeNode> type;
        std::vector<std::shared_ptr<Expression>> args;
        bool is_array{false};
    };
    /** @brief DeleteExpression data structure. */
    struct DeleteExpression : Expression {
        std::shared_ptr<Expression> operand;
        bool is_array{false};
    };
    /** @brief SizeofExpression data structure. */
    struct SizeofExpression : Expression {
        bool is_type{false};
        std::shared_ptr<TypeNode> type_arg;
        std::shared_ptr<Expression> expr_arg;
    };
    /** @brief TypeidExpression data structure. */
    struct TypeidExpression : Expression {
        bool is_type{false};
        std::shared_ptr<TypeNode> type_arg;
        std::shared_ptr<Expression> expr_arg;
    };
    /** @brief DynamicCastExpression data structure. */
    struct DynamicCastExpression : Expression {
        std::shared_ptr<TypeNode> target_type;
        std::shared_ptr<Expression> operand;
    };
    /** @brief StaticCastExpression data structure. */
    struct StaticCastExpression : Expression {
        std::shared_ptr<TypeNode> target_type;
        std::shared_ptr<Expression> operand;
    };
    /** @brief InitializerListExpression data structure. */
    struct InitializerListExpression : Expression {std::vector<std::shared_ptr<Expression>> elements;};
    /** @brief FoldExpression data structure. */
    struct FoldExpression : Expression {
        std::string op;
        std::shared_ptr<Expression> lhs; // may be null for right fold
        std::shared_ptr<Expression> rhs; // may be null for left fold
        bool is_left_fold{false};
        bool is_pack_fold{false};
    };
    /** @brief MemberExpression data structure. */
    struct MemberExpression : Expression {
        std::shared_ptr<Expression> object;
        std::string member;
        bool is_arrow{false};
    };
    /** @brief IndexExpression data structure. */
    struct IndexExpression : Expression {
        std::shared_ptr<Expression> object;
        std::shared_ptr<Expression> index;
    };
    /** @brief CallExpression data structure. */
    struct CallExpression : Expression {
        std::shared_ptr<Expression> callee;
        std::vector<std::shared_ptr<Expression>> args;
    };
    /** @brief ConditionalExpression data structure. */
    struct ConditionalExpression : Expression {
        std::shared_ptr<Expression> condition;
        std::shared_ptr<Expression> then_expr;
        std::shared_ptr<Expression> else_expr;
    };
    /** @brief TemplateIdExpression data structure. */
    struct TemplateIdExpression : Expression {
        std::string name;
        std::vector<std::shared_ptr<Expression>> args;
    };
    /** @brief LambdaExpression data structure. */
    struct LambdaExpression : Expression {
        std::vector<std::string> captures;
        /** @brief Param data structure. */
        struct Param {
            std::string name;
            std::shared_ptr<TypeNode> type;
        };
        std::vector<Param> params;
        std::shared_ptr<TypeNode> return_type;
        std::vector<std::shared_ptr<Statement>> body;
    };
    /** @brief VarDecl data structure. */
    struct VarDecl : Statement {
        std::shared_ptr<TypeNode> type;
        std::string name;
        bool is_constexpr{false};
        bool is_constinit{false};
        bool is_inline{false};
        bool is_static{false};
        std::string access;
        std::vector<Attribute> attributes;
        std::shared_ptr<Expression> init;
    };
    /** @brief ExprStatement data structure. */
    struct ExprStatement : Statement {std::shared_ptr<Expression> expr;};
    /** @brief ReturnStatement data structure. */
    struct ReturnStatement : Statement {
        std::shared_ptr<Expression> value;
        bool is_co_return{false};
    };
    /** @brief IfStatement data structure. */
    struct IfStatement : Statement {
        std::shared_ptr<Expression> condition;
        std::vector<std::shared_ptr<Statement>> then_body;
        std::vector<std::shared_ptr<Statement>> else_body;
        bool is_constexpr{false};
    };
    /** @brief WhileStatement data structure. */
    struct WhileStatement : Statement {
        std::shared_ptr<Expression> condition;
        std::vector<std::shared_ptr<Statement>> body;
    };
    /** @brief ForStatement data structure. */
    struct ForStatement : Statement {
        std::shared_ptr<Statement> init;
        std::shared_ptr<Expression> condition;
        std::shared_ptr<Expression> increment;
        std::vector<std::shared_ptr<Statement>> body;
    };
    /** @brief CompoundStatement data structure. */
    struct CompoundStatement : Statement {std::vector<std::shared_ptr<Statement>> statements;};
    /** @brief BreakStatement data structure. */
    struct BreakStatement : Statement {};
    /** @brief ContinueStatement data structure. */
    struct ContinueStatement : Statement {};
    /** @brief DoWhileStatement data structure. */
    struct DoWhileStatement : Statement {
        std::shared_ptr<Expression> condition;
        std::vector<std::shared_ptr<Statement>> body;
    };
    /** @brief ImportDeclaration data structure. */
    struct ImportDeclaration : Statement {
        std::string module;
        bool is_export{false};
        bool is_header_unit{false};
    };
    /** @brief ModuleDeclaration data structure. */
    struct ModuleDeclaration : Statement {
        std::string name;
        bool is_export{false};
        bool is_partition{false};
        bool is_global{false};
    };
    /** @brief UsingDeclaration data structure. */
    struct UsingDeclaration : Statement {
        std::string name;
        std::string aliased;
    };
    /** @brief UsingEnumDeclaration data structure. */
    struct UsingEnumDeclaration : Statement {
        std::string name;
    };
    /** @brief UsingNamespaceDeclaration data structure. */
    struct UsingNamespaceDeclaration : Statement {std::string ns;};
    /** @brief NamespaceAliasDeclaration data structure. */
    struct NamespaceAliasDeclaration : Statement {
        std::string alias;
        std::string target;
    };
    /** @brief TypedefDeclaration data structure. */
    struct TypedefDeclaration : Statement {
        std::shared_ptr<TypeNode> type;
        std::string alias;
    };
    /** @brief UsingAliasDeclaration data structure. */
    struct UsingAliasDeclaration : Statement {
        std::vector<std::string> template_params;
        std::shared_ptr<TypeNode> aliased_type;
        std::string alias;
    };
    /** @brief FunctionDecl data structure. */
    struct FunctionDecl : Statement {
        /** @brief Param data structure. */
        struct Param {
            std::string name;
            std::shared_ptr<TypeNode> type;
            std::shared_ptr<Expression> default_value;
        };
        std::shared_ptr<TypeNode> return_type;
        std::string name;
        bool is_constexpr{false};
        bool is_consteval{false};
        bool is_inline{false};
        bool is_static{false};
        bool is_virtual{false};
        bool is_override{false};
        bool is_noexcept{false};
        std::shared_ptr<Expression> noexcept_expr;
        bool is_pure_virtual{false};
        bool is_operator{false};
        bool is_constructor{false};
        bool is_destructor{false};
        bool is_defaulted{false};
        bool is_deleted{false};
        bool is_const_qualified{false};
        bool is_friend{false};
        bool is_export{false};
        std::string operator_symbol;
        std::string access;
        std::vector<Attribute> attributes;
        std::vector<Param> params;
        std::vector<std::shared_ptr<Statement>> body;
        std::string requires_clause;
    };
    /** @brief FieldDecl data structure. */
    struct FieldDecl {
        std::shared_ptr<TypeNode> type;
        std::string name;
        std::string access; // public/protected/private (empty for default)
        bool is_static{false};
        bool is_constexpr{false};
        bool is_mutable{false};
        std::vector<Attribute> attributes;
    };
    /** @brief BaseSpecifier data structure. */
    struct BaseSpecifier {
        std::string access; // public/protected/private
        std::string name;
        bool is_virtual{false};
    };
    /** @brief RecordDecl data structure. */
    struct RecordDecl : Statement {
        std::string kind; // "class" or "struct"
        std::string name;
        std::vector<BaseSpecifier> bases;
        std::vector<FieldDecl> fields;
        std::vector<std::shared_ptr<Statement>> methods;
        bool is_forward{false};
        bool is_export{false};
        std::vector<Attribute> attributes;
    };
    /** @brief EnumDecl data structure. */
    struct EnumDecl : Statement {
        std::string name;
        std::vector<std::string> enumerators;
        bool is_forward{false};
        bool is_scoped{false};
        std::shared_ptr<TypeNode> underlying_type;
        bool is_export{false};
    };
    /** @brief NamespaceDecl data structure. */
    struct NamespaceDecl : Statement {
        std::string name;
        std::vector<std::shared_ptr<Statement>> members;
    };
    /** @brief TemplateDecl data structure. */
    struct TemplateDecl : Statement {
        std::vector<std::string> params;
        std::shared_ptr<Statement> inner;
        std::string requires_clause;
    };
    /** @brief ConceptDecl data structure. */
    struct ConceptDecl : Statement {
        std::string name;
        std::vector<std::string> params;
        std::string constraint;
    };
    /** @brief FriendDecl data structure. */
    struct FriendDecl : Statement {
        std::shared_ptr<Statement> decl;
    };
    /** @brief SwitchCase data structure. */
    struct SwitchCase {
        bool is_default{false};
        std::vector<std::shared_ptr<Expression>> labels; // empty when default
        std::vector<std::shared_ptr<Statement>> body;
    };
    /** @brief SwitchStatement data structure. */
    struct SwitchStatement : Statement {
        std::shared_ptr<Expression> condition;
        std::vector<SwitchCase> cases;
    };
    /** @brief ThrowStatement data structure. */
    struct ThrowStatement : Statement {
        std::shared_ptr<Expression> value;
    };
    /** @brief CatchClause data structure. */
    struct CatchClause {
        std::shared_ptr<TypeNode> exception_type;
        std::string name;
        std::vector<std::shared_ptr<Statement>> body;
    };
    /** @brief TryStatement data structure. */
    struct TryStatement : Statement {
        std::vector<std::shared_ptr<Statement>> try_body;
        std::vector<CatchClause> catches;
        std::vector<std::shared_ptr<Statement>> finally_body; // unused for C++, kept for parity
    };
    /** @brief StructuredBindingDecl data structure. */
    struct StructuredBindingDecl : Statement {
        std::shared_ptr<TypeNode> type;
        std::vector<std::string> names;
        std::shared_ptr<Expression> init;
    };
    /** @brief RangeForStatement data structure. */
    struct RangeForStatement : Statement {
        std::shared_ptr<Statement> loop_var;
        std::shared_ptr<Expression> range;
        std::vector<std::shared_ptr<Statement>> body;
    };
    /** @brief ForwardDecl data structure. */
    struct ForwardDecl : Statement {
        std::string kind; // class/struct/enum
        std::string name;
    };
    /** @brief Module data structure. */
    struct Module : AstNode {std::vector<std::shared_ptr<Statement>> declarations;};

} // namespace polyglot::cpp
