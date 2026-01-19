#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::rust {

struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
};

struct Statement : AstNode {};

struct Expression : AstNode {};

struct Pattern : AstNode {};

struct IdentifierPattern : Pattern {
    std::string name;
};

struct WildcardPattern : Pattern {};

struct TuplePattern : Pattern {
    std::vector<std::shared_ptr<Pattern>> elements;
};

struct LiteralPattern : Pattern {
    std::string value;
};

struct PathPattern : Pattern {
    bool is_absolute{false};
    std::vector<std::string> segments;
};

struct StructPattern : Pattern {
    PathPattern path;
    std::vector<std::string> fields;
    bool has_rest{false};
};

struct Identifier : Expression {
    std::string name;
};

struct Literal : Expression {
    std::string value;
};

struct PathExpression : Expression {
    bool is_absolute{false};
    std::vector<std::string> segments;
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
};

struct IndexExpression : Expression {
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
};

struct RangeExpression : Expression {
    std::shared_ptr<Expression> start;
    std::shared_ptr<Expression> end;
    bool inclusive{false};
};

struct CallExpression : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<std::shared_ptr<Expression>> args;
};

struct ClosureExpression : Expression {
    bool is_move{false};
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::vector<Param> params;
    std::shared_ptr<Expression> body;
};

struct MacroCallExpression : Expression {
    PathExpression path;
    std::string delimiter;
    std::string body;
};

struct AssignmentExpression : Expression {
    std::string op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

struct TypeNode : AstNode {};

struct TypePath : TypeNode {
    bool is_absolute{false};
    std::vector<std::string> segments;
    std::vector<std::vector<std::shared_ptr<TypeNode>>> generic_args;
};

struct LifetimeType : TypeNode {
    std::string name;
};

struct ReferenceType : TypeNode {
    bool is_mut{false};
    std::shared_ptr<LifetimeType> lifetime;
    std::shared_ptr<TypeNode> inner;
};

struct SliceType : TypeNode {
    std::shared_ptr<TypeNode> inner;
};

struct ArrayType : TypeNode {
    std::shared_ptr<TypeNode> inner;
    std::string size_expr;
};

struct TupleType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> elements;
};

struct FunctionType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> params;
    std::shared_ptr<TypeNode> return_type;
};

struct LetStatement : Statement {
    bool is_mut{false};
    std::shared_ptr<Pattern> pattern;
    std::shared_ptr<Expression> init;
    std::shared_ptr<TypeNode> type_annotation;
};

struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

struct BreakStatement : Statement {
    std::shared_ptr<Expression> value;
};

struct ContinueStatement : Statement {};

struct LoopStatement : Statement {
    std::vector<std::shared_ptr<Statement>> body;
};

struct ForStatement : Statement {
    std::shared_ptr<Pattern> pattern;
    std::shared_ptr<Expression> iterable;
    std::vector<std::shared_ptr<Statement>> body;
};

struct IfExpression : Expression {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> then_body;
    std::vector<std::shared_ptr<Statement>> else_body;
};

struct WhileExpression : Expression {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> body;
};

struct BlockExpression : Expression {
    std::vector<std::shared_ptr<Statement>> statements;
};

struct MatchArm : AstNode {
    std::shared_ptr<Pattern> pattern;
    std::shared_ptr<Expression> guard;
    std::shared_ptr<Expression> body;
};

struct MatchExpression : Expression {
    std::shared_ptr<Expression> scrutinee;
    std::vector<std::shared_ptr<MatchArm>> arms;
};

struct UseDeclaration : Statement {
    std::string path;
};

struct ImplItem : Statement {
    std::shared_ptr<TypeNode> target_type;
    std::vector<std::shared_ptr<Statement>> items;
};

struct TraitItem : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> items;
};

struct ModItem : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> items;
};

struct FunctionItem : Statement {
    std::string name;
    std::vector<std::string> type_params;
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::vector<Param> params;
    std::shared_ptr<TypeNode> return_type;
    std::vector<std::shared_ptr<Statement>> body;
};

struct StructItem : Statement {
    std::string name;
    std::vector<std::string> fields;
};

struct EnumItem : Statement {
    std::string name;
    std::vector<std::string> variants;
};

struct Module : AstNode {
    std::vector<std::shared_ptr<Statement>> items;
};

} // namespace polyglot::rust
