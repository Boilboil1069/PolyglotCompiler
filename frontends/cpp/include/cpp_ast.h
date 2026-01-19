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

struct TypeNode : AstNode {};

struct SimpleType : TypeNode {
    std::string name;
};

struct PointerType : TypeNode {
    std::shared_ptr<TypeNode> pointee;
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
};

struct Literal : Expression {
    std::string value;
};

struct UnaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> operand;
};

struct BinaryExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
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
    std::shared_ptr<Expression> init;
};

struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

struct IfStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> then_body;
    std::vector<std::shared_ptr<Statement>> else_body;
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

struct CompoundStatement : Statement {
    std::vector<std::shared_ptr<Statement>> statements;
};

struct ImportDeclaration : Statement {
    std::string module;
};

struct UsingDeclaration : Statement {
    std::string name;
    std::string aliased;
};

struct FunctionDecl : Statement {
    std::shared_ptr<TypeNode> return_type;
    std::string name;
    std::vector<std::pair<std::string, std::shared_ptr<TypeNode>>> params;
    std::vector<std::shared_ptr<Statement>> body;
};

struct FieldDecl {
    std::shared_ptr<TypeNode> type;
    std::string name;
};

struct RecordDecl : Statement {
    std::string kind; // "class" or "struct"
    std::string name;
    std::vector<FieldDecl> fields;
    std::vector<std::shared_ptr<Statement>> methods;
};

struct EnumDecl : Statement {
    std::string name;
    std::vector<std::string> enumerators;
};

struct NamespaceDecl : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> members;
};

struct TemplateDecl : Statement {
    std::vector<std::string> params;
    std::shared_ptr<Statement> inner;
};

struct Module : AstNode {
    std::vector<std::shared_ptr<Statement>> declarations;
};

} // namespace polyglot::cpp
