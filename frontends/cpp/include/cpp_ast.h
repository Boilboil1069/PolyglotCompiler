#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::cpp {

    struct AstNode {
        virtual ~AstNode() = default;
        core::SourceLoc loc{};
    };
    struct Statement : AstNode {};
    struct Expression : AstNode {};
    struct ConditionalExpression;
    struct TemplateIdExpression;
    struct TypeNode : AstNode {};
    struct Attribute {std::string text;};
    struct SimpleType : TypeNode {std::string name;};
    struct PointerType : TypeNode {
        std::shared_ptr<TypeNode> pointee;
        bool is_const{false};
        bool is_volatile{false};
    };
    struct ReferenceType : TypeNode {
        std::shared_ptr<TypeNode> referent;
        bool is_rvalue{false};
    };
    struct ArrayType : TypeNode {
        std::shared_ptr<TypeNode> element_type;
        std::string size_expr;
    };
    struct FunctionType : TypeNode {
        std::shared_ptr<TypeNode> return_type;
        std::vector<std::shared_ptr<TypeNode>> params;
    };
    struct QualifiedType : TypeNode {
        bool is_const{false};
        bool is_volatile{false};
        std::shared_ptr<TypeNode> inner;
    };
    struct Identifier : Expression {
        std::string name;
        std::vector<std::shared_ptr<Expression>> template_args;
    };
    struct Literal : Expression {std::string value;};
    struct UnaryExpression : Expression {
        std::string op;
        std::shared_ptr<Expression> operand;
    };
    struct BinaryExpression : Expression {
        std::string op;
        std::shared_ptr<Expression> left;
        std::shared_ptr<Expression> right;
    };
    struct NewExpression : Expression {
        std::shared_ptr<TypeNode> type;
        std::vector<std::shared_ptr<Expression>> args;
        bool is_array{false};
    };
    struct DeleteExpression : Expression {
        std::shared_ptr<Expression> operand;
        bool is_array{false};
    };
    struct SizeofExpression : Expression {
        bool is_type{false};
        std::shared_ptr<TypeNode> type_arg;
        std::shared_ptr<Expression> expr_arg;
    };
    struct TypeidExpression : Expression {
        bool is_type{false};
        std::shared_ptr<TypeNode> type_arg;
        std::shared_ptr<Expression> expr_arg;
    };
    struct DynamicCastExpression : Expression {
        std::shared_ptr<TypeNode> target_type;
        std::shared_ptr<Expression> operand;
    };
    struct StaticCastExpression : Expression {
        std::shared_ptr<TypeNode> target_type;
        std::shared_ptr<Expression> operand;
    };
    struct InitializerListExpression : Expression {std::vector<std::shared_ptr<Expression>> elements;};
    struct FoldExpression : Expression {
        std::string op;
        std::shared_ptr<Expression> lhs; // may be null for right fold
        std::shared_ptr<Expression> rhs; // may be null for left fold
        bool is_left_fold{false};
        bool is_pack_fold{false};
    };
    struct MemberExpression : Expression {
        std::shared_ptr<Expression> object;
        std::string member;
        bool is_arrow{false};
    };
    struct IndexExpression : Expression {
        std::shared_ptr<Expression> object;
        std::shared_ptr<Expression> index;
    };
    struct CallExpression : Expression {
        std::shared_ptr<Expression> callee;
        std::vector<std::shared_ptr<Expression>> args;
    };
    struct ConditionalExpression : Expression {
        std::shared_ptr<Expression> condition;
        std::shared_ptr<Expression> then_expr;
        std::shared_ptr<Expression> else_expr;
    };
    struct TemplateIdExpression : Expression {
        std::string name;
        std::vector<std::shared_ptr<Expression>> args;
    };
    struct LambdaExpression : Expression {
        std::vector<std::string> captures;
        struct Param {
            std::string name;
            std::shared_ptr<TypeNode> type;
        };
        std::vector<Param> params;
        std::shared_ptr<TypeNode> return_type;
        std::vector<std::shared_ptr<Statement>> body;
    };
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
    struct ExprStatement : Statement {std::shared_ptr<Expression> expr;};
    struct ReturnStatement : Statement {
        std::shared_ptr<Expression> value;
        bool is_co_return{false};
    };
    struct IfStatement : Statement {
        std::shared_ptr<Expression> condition;
        std::vector<std::shared_ptr<Statement>> then_body;
        std::vector<std::shared_ptr<Statement>> else_body;
        bool is_constexpr{false};
    };
    struct WhileStatement : Statement {
        std::shared_ptr<Expression> condition;
        std::vector<std::shared_ptr<Statement>> body;
    };
    struct ForStatement : Statement {
        std::shared_ptr<Statement> init;
        std::shared_ptr<Expression> condition;
        std::shared_ptr<Expression> increment;
        std::vector<std::shared_ptr<Statement>> body;
    };
    struct CompoundStatement : Statement {std::vector<std::shared_ptr<Statement>> statements;};
    struct ImportDeclaration : Statement {
        std::string module;
        bool is_export{false};
        bool is_header_unit{false};
    };
    struct ModuleDeclaration : Statement {
        std::string name;
        bool is_export{false};
        bool is_partition{false};
        bool is_global{false};
    };
    struct UsingDeclaration : Statement {
        std::string name;
        std::string aliased;
    };
    struct UsingEnumDeclaration : Statement {
        std::string name;
    };
    struct UsingNamespaceDeclaration : Statement {std::string ns;};
    struct NamespaceAliasDeclaration : Statement {
        std::string alias;
        std::string target;
    };
    struct TypedefDeclaration : Statement {
        std::shared_ptr<TypeNode> type;
        std::string alias;
    };
    struct UsingAliasDeclaration : Statement {
        std::vector<std::string> template_params;
        std::shared_ptr<TypeNode> aliased_type;
        std::string alias;
    };
    struct FunctionDecl : Statement {
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
    struct FieldDecl {
        std::shared_ptr<TypeNode> type;
        std::string name;
        std::string access; // public/protected/private (empty for default)
        bool is_static{false};
        bool is_constexpr{false};
        bool is_mutable{false};
        std::vector<Attribute> attributes;
    };
    struct BaseSpecifier {
        std::string access; // public/protected/private
        std::string name;
        bool is_virtual{false};
    };
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
    struct EnumDecl : Statement {
        std::string name;
        std::vector<std::string> enumerators;
        bool is_forward{false};
        bool is_scoped{false};
        std::shared_ptr<TypeNode> underlying_type;
        bool is_export{false};
    };
    struct NamespaceDecl : Statement {
        std::string name;
        std::vector<std::shared_ptr<Statement>> members;
    };
    struct TemplateDecl : Statement {
        std::vector<std::string> params;
        std::shared_ptr<Statement> inner;
        std::string requires_clause;
    };
    struct ConceptDecl : Statement {
        std::string name;
        std::vector<std::string> params;
        std::string constraint;
    };
    struct FriendDecl : Statement {
        std::shared_ptr<Statement> decl;
    };
    struct SwitchCase {
        bool is_default{false};
        std::vector<std::shared_ptr<Expression>> labels; // empty when default
        std::vector<std::shared_ptr<Statement>> body;
    };
    struct SwitchStatement : Statement {
        std::shared_ptr<Expression> condition;
        std::vector<SwitchCase> cases;
    };
    struct ThrowStatement : Statement {
        std::shared_ptr<Expression> value;
    };
    struct CatchClause {
        std::shared_ptr<TypeNode> exception_type;
        std::string name;
        std::vector<std::shared_ptr<Statement>> body;
    };
    struct TryStatement : Statement {
        std::vector<std::shared_ptr<Statement>> try_body;
        std::vector<CatchClause> catches;
        std::vector<std::shared_ptr<Statement>> finally_body; // unused for C++, kept for parity
    };
    struct StructuredBindingDecl : Statement {
        std::shared_ptr<TypeNode> type;
        std::vector<std::string> names;
        std::shared_ptr<Expression> init;
    };
    struct RangeForStatement : Statement {
        std::shared_ptr<Statement> loop_var;
        std::shared_ptr<Expression> range;
        std::vector<std::shared_ptr<Statement>> body;
    };
    struct ForwardDecl : Statement {
        std::string kind; // class/struct/enum
        std::string name;
    };
    struct Module : AstNode {std::vector<std::shared_ptr<Statement>> declarations;};

} // namespace polyglot::cpp
