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

struct CallExpression : Expression {
  std::shared_ptr<Expression> callee;
  std::vector<std::shared_ptr<Expression>> args;
};

struct ExprStatement : Statement {
  std::shared_ptr<Expression> expr;
};

struct ReturnStatement : Statement {
  std::shared_ptr<Expression> value;
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
