#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::python {

struct AstNode {
    virtual ~AstNode() = default;
    core::SourceLoc loc{};
};

struct Statement : AstNode {};

struct Expression : AstNode {};

struct Parameter {
    std::string name;
    std::shared_ptr<Expression> annotation;
    std::shared_ptr<Expression> default_value;
};

struct Identifier : Expression {
    std::string name;
};

struct Literal : Expression {
    std::string value;
};

struct TupleExpression : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
    bool is_parenthesized{false};
};

struct ListExpression : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
};

struct SetExpression : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
};

struct DictExpression : Expression {
    std::vector<std::pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>>> items;
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

struct NamedExpression : Expression {
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> value;
};

struct AwaitExpression : Expression {
    std::shared_ptr<Expression> value;
};

struct YieldExpression : Expression {
    std::shared_ptr<Expression> value; // nullptr means bare yield
    bool is_from{false};
};

struct SliceExpression : Expression {
    std::shared_ptr<Expression> start;
    std::shared_ptr<Expression> stop;
    std::shared_ptr<Expression> step;
};

struct AttributeExpression : Expression {
    std::shared_ptr<Expression> object;
    std::string attribute;
};

struct IndexExpression : Expression {
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
};

struct Comprehension {
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> iterable;
    std::vector<std::shared_ptr<Expression>> ifs;
};

struct ComprehensionExpression : Expression {
    enum class Kind { kList, kSet, kDict, kGenerator } kind{Kind::kList};
    std::shared_ptr<Expression> elem;   // for list/set/generator value
    std::shared_ptr<Expression> key;    // for dict key
    std::vector<Comprehension> clauses;
};

struct CallArg {
    std::string keyword; // empty for positional
    std::shared_ptr<Expression> value;
    bool is_star{false};
    bool is_kwstar{false};
};

struct CallExpression : Expression {
    std::shared_ptr<Expression> callee;
    std::vector<CallArg> args;
};

struct LambdaExpression : Expression {
    std::vector<Parameter> params;
    std::shared_ptr<Expression> body;
};

struct Assignment : Statement {
    std::vector<std::shared_ptr<Expression>> targets;
    std::shared_ptr<Expression> annotation;
    std::shared_ptr<Expression> value;
};

struct ExprStatement : Statement {
    std::shared_ptr<Expression> expr;
};

struct ReturnStatement : Statement {
    std::shared_ptr<Expression> value;
};

struct PassStatement : Statement {};

struct BreakStatement : Statement {};

struct ContinueStatement : Statement {};

struct RaiseStatement : Statement {
    std::shared_ptr<Expression> value;
    std::shared_ptr<Expression> from_expr;
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
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> iterable;
    std::vector<std::shared_ptr<Statement>> body;
};

struct WithItem {
    std::shared_ptr<Expression> context_expr;
    std::shared_ptr<Expression> optional_vars;
};

struct WithStatement : Statement {
    bool is_async{false};
    std::vector<WithItem> items;
    std::vector<std::shared_ptr<Statement>> body;
};

struct ExceptHandler {
    std::shared_ptr<Expression> type;
    std::string name;
    std::vector<std::shared_ptr<Statement>> body;
};

struct TryStatement : Statement {
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<ExceptHandler> handlers;
    std::vector<std::shared_ptr<Statement>> orelse;
    std::vector<std::shared_ptr<Statement>> finalbody;
};

struct MatchCase {
    std::shared_ptr<Expression> pattern;
    std::shared_ptr<Expression> guard;
    std::vector<std::shared_ptr<Statement>> body;
};

struct MatchStatement : Statement {
    std::shared_ptr<Expression> subject;
    std::vector<MatchCase> cases;
};

struct GlobalStatement : Statement {
    std::vector<std::string> names;
};

struct NonlocalStatement : Statement {
    std::vector<std::string> names;
};

struct AssertStatement : Statement {
    std::shared_ptr<Expression> test;
    std::shared_ptr<Expression> msg;
};

struct ImportStatement : Statement {
    bool is_from{false};
    std::string module;
    bool is_star{false};
    struct Alias {
        std::string name;
        std::string alias;
    };
    std::vector<Alias> names;
};

struct FunctionDef : Statement {
    std::string name;
    std::vector<Parameter> params;
    std::shared_ptr<Expression> return_annotation;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<std::shared_ptr<Expression>> decorators;
    bool is_async{false};
};

struct ClassDef : Statement {
    std::string name;
    std::vector<std::shared_ptr<Expression>> bases;
    std::vector<std::shared_ptr<Statement>> body;
    std::vector<std::shared_ptr<Expression>> decorators;
};

struct Module : AstNode {
    std::vector<std::shared_ptr<Statement>> body;
};

} // namespace polyglot::python
