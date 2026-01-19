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

struct VarDecl : Statement {
  std::string type_name;
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

struct FunctionDecl : Statement {
  std::string return_type;
  std::string name;
  std::vector<std::string> params;
  std::vector<std::shared_ptr<Statement>> body;
};

struct Module : AstNode {
  std::vector<std::shared_ptr<Statement>> declarations;
};

}  // namespace polyglot::cpp
