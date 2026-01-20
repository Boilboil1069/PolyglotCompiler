#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::rust {

struct Attribute {
    std::string text;
    bool is_inner{false};
};

struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
    std::vector<Attribute> attributes;
};

struct Statement : AstNode {};

struct Expression : AstNode {};

struct Pattern : AstNode {};

struct TypeNode : AstNode {};

struct IdentifierPattern : Pattern {
    std::string name;
    bool is_ref{false};
    bool is_mut{false};
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

struct OrPattern : Pattern {
    std::vector<std::shared_ptr<Pattern>> patterns;
};

struct RefPattern : Pattern {
    bool is_mut{false};
    std::shared_ptr<Pattern> inner;
};

struct RangePattern : Pattern {
    std::shared_ptr<Pattern> start;
    std::shared_ptr<Pattern> end;
    bool inclusive{false};
};

struct SlicePattern : Pattern {
    std::vector<std::shared_ptr<Pattern>> elements;
    bool has_rest{false};
};

struct TupleStructPattern : Pattern {
    PathPattern path;
    std::vector<std::shared_ptr<Pattern>> elements;
};

struct BindingPattern : Pattern {
    std::string name;
    bool is_ref{false};
    bool is_mut{false};
    std::shared_ptr<Pattern> pattern;
};

struct StructPatternField {
    std::string name;
    std::shared_ptr<Pattern> pattern;
    bool is_shorthand{false};
};

struct StructPattern : Pattern {
    PathPattern path;
    std::vector<StructPatternField> fields;
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
    std::vector<std::vector<std::shared_ptr<TypeNode>>> generic_args;
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
    std::vector<std::shared_ptr<TypeNode>> generic_args;
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

struct AwaitExpression : Expression {
    std::shared_ptr<Expression> value;
};

struct TryExpression : Expression {
    std::shared_ptr<Expression> value;
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

struct TypePath : TypeNode {
    bool is_absolute{false};
    std::vector<std::string> segments;
    std::vector<std::vector<std::shared_ptr<TypeNode>>> generic_args;
};

struct ConstExprType : TypeNode {
    std::string expr;
};

struct TraitObjectType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> bounds;
};

struct ImplTraitType : TypeNode {
    std::vector<std::shared_ptr<TypeNode>> bounds;
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
    std::string visibility;
};

struct ImplItem : Statement {
    std::string visibility;
    bool is_unsafe{false};
    std::vector<std::string> type_params;
    std::shared_ptr<TypeNode> target_type;
    std::shared_ptr<TypeNode> trait_type;
    std::string where_clause;
    std::vector<std::shared_ptr<Statement>> items;
};

struct TraitItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<std::string> type_params;
    std::vector<std::shared_ptr<TypeNode>> super_traits;
    std::string where_clause;
    std::vector<std::shared_ptr<Statement>> items;
};

struct ModItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<std::shared_ptr<Statement>> items;
};

struct FunctionItem : Statement {
    std::string name;
    std::vector<std::string> type_params;
    std::string visibility;
    bool is_async{false};
    bool is_const{false};
    bool is_unsafe{false};
    bool is_extern{false};
    std::string extern_abi;
    std::string where_clause;
    bool has_body{true};
    struct Param {
        std::string name;
        std::shared_ptr<TypeNode> type;
    };
    std::vector<Param> params;
    std::shared_ptr<TypeNode> return_type;
    std::vector<std::shared_ptr<Statement>> body;
};

struct StructField {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::string visibility;
};

struct StructItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<StructField> fields;
    bool is_tuple{false};
    bool is_unit{false};
};

struct EnumVariant {
    std::string name;
    std::vector<std::shared_ptr<TypeNode>> tuple_fields;
    std::vector<StructField> struct_fields;
    std::shared_ptr<Expression> discriminant;
};

struct EnumItem : Statement {
    std::string name;
    std::string visibility;
    std::vector<EnumVariant> variants;
};

struct ConstItem : Statement {
    std::string name;
    std::shared_ptr<TypeNode> type;
    std::shared_ptr<Expression> value;
    std::string visibility;
};

struct TypeAliasItem : Statement {
    std::string name;
    std::shared_ptr<TypeNode> alias;
    std::string visibility;
};

struct MacroRulesItem : Statement {
    std::string name;
    std::string body;
};

struct Module : AstNode {
    std::vector<std::shared_ptr<Statement>> items;
};

} // namespace polyglot::rust
