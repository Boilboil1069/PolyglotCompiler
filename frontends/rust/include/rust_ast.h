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
};

struct IndexExpression : Expression {
  std::shared_ptr<Expression> object;
  std::shared_ptr<Expression> index;
};

struct CallExpression : Expression {
  std::shared_ptr<Expression> callee;
  std::vector<std::shared_ptr<Expression>> args;
};

struct LetStatement : Statement {
  std::string name;
  std::shared_ptr<Expression> init;
};

struct ExprStatement : Statement {
  std::shared_ptr<Expression> expr;
};

struct ReturnStatement : Statement {
  std::shared_ptr<Expression> value;
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

struct UseDeclaration : Statement {
  std::string path;
};

struct FunctionItem : Statement {
  std::string name;
  std::vector<std::string> params;
  std::vector<std::shared_ptr<Statement>> body;
};

struct Module : AstNode {
  std::vector<std::shared_ptr<Statement>> items;
};

}  // namespace polyglot::rust
