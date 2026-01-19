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

struct AttributeExpression : Expression {
  std::shared_ptr<Expression> object;
  std::string attribute;
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
  std::vector<Parameter> params;
  std::shared_ptr<Expression> body;
};

struct Assignment : Statement {
  std::shared_ptr<Expression> target;
  std::shared_ptr<Expression> annotation;
  std::shared_ptr<Expression> value;
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
  std::shared_ptr<Expression> target;
  std::shared_ptr<Expression> iterable;
  std::vector<std::shared_ptr<Statement>> body;
};

struct ImportStatement : Statement {
  bool is_from{false};
  std::string module;
  std::string name;
  std::string alias;
};

struct FunctionDef : Statement {
  std::string name;
  std::vector<Parameter> params;
  std::shared_ptr<Expression> return_annotation;
  std::vector<std::shared_ptr<Statement>> body;
};

struct ClassDef : Statement {
  std::string name;
  std::vector<std::shared_ptr<Expression>> bases;
  std::vector<std::shared_ptr<Statement>> body;
};

struct Module : AstNode {
  std::vector<std::shared_ptr<Statement>> body;
};

}  // namespace polyglot::python
