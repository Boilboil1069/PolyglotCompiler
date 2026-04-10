/**
 * @file     python_ast.h
 * @brief    Python language frontend
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::python {

/** @brief AstNode data structure. */
struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
    std::string doc;
};

/** @brief Statement data structure. */
struct Statement : AstNode {};

/** @brief Expression data structure. */
struct Expression : AstNode {};

/** @brief Parameter data structure. */
struct Parameter {
    std::string name;
    std::shared_ptr<Expression> annotation;
    std::shared_ptr<Expression> default_value;
    bool is_vararg{false};
    bool is_kwarg{false};
    bool is_kwonly{false};
};

/** @brief Identifier data structure. */
struct Identifier : Expression {
    std::string name;
};

/** @brief Literal data structure. */
struct Literal : Expression {
    std::string value;
    bool is_string{false};
};

/** @brief TupleExpression data structure. */
struct TupleExpression : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
    bool is_parenthesized{false};
};

/** @brief ListExpression data structure. */
struct ListExpression : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
};

/** @brief SetExpression data structure. */
struct SetExpression : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
};

/** @brief DictExpression data structure. */
struct DictExpression : Expression {
    std::vector<std::pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>>> items;
};

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

/** @brief NamedExpression data structure. */
struct NamedExpression : Expression {
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> value;
};

/** @brief AwaitExpression data structure. */
struct AwaitExpression : Expression {
    std::shared_ptr<Expression> value;
};

/** @brief YieldExpression data structure. */
struct YieldExpression : Expression {
    std::shared_ptr<Expression> value; // nullptr means bare yield
    bool is_from{false};
};

/** @brief SliceExpression data structure. */
struct SliceExpression : Expression {
    std::shared_ptr<Expression> start;
    std::shared_ptr<Expression> stop;
    std::shared_ptr<Expression> step;
};

/** @brief AttributeExpression data structure. */
struct AttributeExpression : Expression {
    std::shared_ptr<Expression> object;
    std::string attribute;
};

/** @brief IndexExpression data structure. */
struct IndexExpression : Expression {
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
};

/** @brief Comprehension data structure. */
struct Comprehension {
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> iterable;
    std::vector<std::shared_ptr<Expression>> ifs;
};

/** @brief ComprehensionExpression data structure. */
struct ComprehensionExpression : Expression {
    /** @brief Kind enumeration. */
    enum class Kind { kList, kSet, kDict, kGenerator } kind{Kind::kList};
    std::shared_ptr<Expression> elem;   // for list/set/generator value
    std::shared_ptr<Expression> key;    // for dict key
    std::vector<Comprehension> clauses;
};

/** @brief CallArg data structure. */
struct CallArg {
    std::string keyword; // empty for positional
    std::shared_ptr<Expression> value;
    bool is_star{false};
    bool is_kwstar{false};
};

/** @brief CallExpression data structure. */
struct CallExpression : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<CallArg> args;
};

/** @brief LambdaExpression data structure. */
struct LambdaExpression : Expression {
    std::vector<Parameter> params;
    std::shared_ptr<Expression> body;
};

/** @brief Assignment data structure. */
struct Assignment : Statement {
    std::string op{"="};
    std::vector<std::shared_ptr<Expression>> targets;
    std::shared_ptr<Expression> annotation;
    std::shared_ptr<Expression> value;
};

/** @brief ExprStatement data structure. */
struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

/** @brief ReturnStatement data structure. */
struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

/** @brief PassStatement data structure. */
struct PassStatement : Statement {};

/** @brief BreakStatement data structure. */
struct BreakStatement : Statement {};

/** @brief ContinueStatement data structure. */
struct ContinueStatement : Statement {};

/** @brief RaiseStatement data structure. */
struct RaiseStatement : Statement {
    std::shared_ptr<Expression> value;
    std::shared_ptr<Expression> from_expr;
};

/** @brief IfStatement data structure. */
struct IfStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> then_body;
    std::vector<std::shared_ptr<Statement>> else_body;
};

/** @brief WhileStatement data structure. */
struct WhileStatement : Statement {
    std::shared_ptr<Expression> condition;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief ForStatement data structure. */
struct ForStatement : Statement {
    bool is_async{false};
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> iterable;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief WithItem data structure. */
struct WithItem {
    std::shared_ptr<Expression> context_expr;
    std::shared_ptr<Expression> optional_vars;
};

/** @brief WithStatement data structure. */
struct WithStatement : Statement {
    bool is_async{false};
    std::vector<WithItem> items;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief ExceptHandler data structure. */
struct ExceptHandler {
    std::shared_ptr<Expression> type;
    std::string name;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief TryStatement data structure. */
struct TryStatement : Statement {
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<ExceptHandler> handlers;
    std::vector<std::shared_ptr<Statement>> orelse;
    std::vector<std::shared_ptr<Statement>> finalbody;
};

/** @brief MatchCase data structure. */
struct MatchCase {
    std::shared_ptr<Expression> pattern;
    std::shared_ptr<Expression> guard;
    std::vector<std::shared_ptr<Statement>> body;
};

/** @brief MatchStatement data structure. */
struct MatchStatement : Statement {
    std::shared_ptr<Expression> subject;
    std::vector<MatchCase> cases;
};

/** @brief GlobalStatement data structure. */
struct GlobalStatement : Statement {
    std::vector<std::string> names;
};

/** @brief NonlocalStatement data structure. */
struct NonlocalStatement : Statement {
    std::vector<std::string> names;
};

/** @brief AssertStatement data structure. */
struct AssertStatement : Statement {
    std::shared_ptr<Expression> test;
    std::shared_ptr<Expression> msg;
};

/** @brief ImportStatement data structure. */
struct ImportStatement : Statement {
    bool is_from{false};
    std::string module;
    bool is_star{false};
    /** @brief Alias data structure. */
    struct Alias {
        std::string name;
        std::string alias;
    };
    std::vector<Alias> names;
};

/** @brief FunctionDef data structure. */
struct FunctionDef : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::shared_ptr<Expression> return_annotation;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<std::shared_ptr<Expression>> decorators;
    bool is_async{false};
};

/** @brief ClassDef data structure. */
struct ClassDef : Statement {
    std::string name;
    std::vector<std::shared_ptr<Expression>> bases;
    std::vector<CallArg> keywords;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<std::shared_ptr<Expression>> decorators;
};

// Advanced Python language features consolidated here to avoid separate headers.

/** @brief Decorator data structure. */
struct Decorator : AstNode {
    std::string name;
    std::vector<std::shared_ptr<Expression>> args;
};

/** @brief AsyncFunctionDef data structure. */
struct AsyncFunctionDef : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::vector<std::shared_ptr<Statement>> body;
    bool is_async{true};
};

/** @brief ComprehensionClause data structure. */
struct ComprehensionClause {
    std::string var;
    std::shared_ptr<Expression> iter;
    std::vector<std::shared_ptr<Expression>> conditions;
};

/** @brief ListComprehension data structure. */
struct ListComprehension : Expression {
    std::shared_ptr<Expression> element;
    std::vector<ComprehensionClause> clauses;
};

/** @brief DictComprehension data structure. */
struct DictComprehension : Expression {
    std::shared_ptr<Expression> key;
    std::shared_ptr<Expression> value;
    std::vector<ComprehensionClause> clauses;
};

/** @brief SetComprehension data structure. */
struct SetComprehension : Expression {
    std::shared_ptr<Expression> element;
    std::vector<ComprehensionClause> clauses;
};

/** @brief DataclassDecl data structure. */
struct DataclassDecl : ClassDef {
    bool frozen{false};
    bool order{false};
    bool slots{false};
    bool init{true};
    bool repr{true};
    bool eq{true};
};

/** @brief PropertyDecl data structure. */
struct PropertyDecl : AstNode {
    std::shared_ptr<FunctionDef> getter;
    std::shared_ptr<FunctionDef> setter;
    std::shared_ptr<FunctionDef> deleter;
    std::string doc;
};

/** @brief MethodKind data structure. */
struct MethodKind {
    /** @brief Kind enumeration. */
    enum Kind { kInstance, kStatic, kClass };
    Kind kind{kInstance};
};

/** @brief InheritanceInfo data structure. */
struct InheritanceInfo {
    std::vector<std::string> base_classes;
    std::vector<std::string> mro;
};

/** @brief MetaclassInfo data structure. */
struct MetaclassInfo {
    std::string metaclass_name;
    std::vector<std::shared_ptr<Expression>> kwargs;
};

/** @brief DescriptorInfo data structure. */
struct DescriptorInfo {
    bool has_get{false};
    bool has_set{false};
    bool has_delete{false};
};

/** @brief AnnotatedAssignment data structure. */
struct AnnotatedAssignment : Statement {
    std::string target;
    std::shared_ptr<Expression> value;
};

/** @brief FormattedString data structure. */
struct FormattedString : Expression {
    /** @brief Part data structure. */
    struct Part {
        bool is_literal{true};
        std::string literal;
        std::shared_ptr<Expression> expr;
        std::string format_spec;
    };
    std::vector<Part> parts;
};

/** @brief WalrusExpression data structure. */
struct WalrusExpression : Expression {
    std::string target;
    std::shared_ptr<Expression> value;
};

/** @brief TypeHint data structure. */
struct TypeHint : AstNode {
    /** @brief HintKind enumeration. */
    enum HintKind {
        kSimple,
        kGeneric,
        kUnion,
        kOptional,
        kCallable,
        kLiteral,
        kTypeVar,
        kProtocol,
        kAnnotated
    };

    HintKind hint_kind{kSimple};
    std::string name;
    std::vector<std::shared_ptr<TypeHint>> args;
};

/** @brief Module data structure. */
struct Module : AstNode {
    std::vector<std::shared_ptr<Statement>> body;
};

} // namespace polyglot::python
