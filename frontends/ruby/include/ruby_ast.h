/**
 * @file     ruby_ast.h
 * @brief    Ruby AST definitions
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Covers the subset of Ruby (2.7 / 3.x) required by the polyglot compiler:
 *   - Top-level methods (`def`)
 *   - Classes / modules with method definitions
 *   - YARD-style type tags (`# @param [Integer] x`, `# @return [String]`)
 *   - Local variables, assignments, block expressions
 *   - if / unless / while / until / case / for / begin-rescue-ensure
 *   - Method calls (with and without parentheses), block arguments
 *   - Literal numeric/string/symbol/boolean/nil/array/hash forms
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::ruby {

struct AstNode { virtual ~AstNode() = default; core::SourceLoc loc{}; };
struct Statement  : AstNode {};
struct Expression : AstNode {};
struct TypeNode   : AstNode { std::string name; std::vector<std::shared_ptr<TypeNode>> args; };

// ---- Expressions -----------------------------------------------------------

struct Identifier : Expression { std::string name; };
struct Literal : Expression {
    enum class Kind { kInt, kFloat, kString, kSymbol, kBool, kNil, kRegex };
    Kind kind{Kind::kInt};
    std::string value;
};
struct ArrayLit  : Expression { std::vector<std::shared_ptr<Expression>> elems; };
struct HashLit   : Expression {
    struct Pair { std::shared_ptr<Expression> key, value; };
    std::vector<Pair> pairs;
};
struct UnaryExpr  : Expression { std::string op; std::shared_ptr<Expression> operand; };
struct BinaryExpr : Expression { std::string op; std::shared_ptr<Expression> left, right; };
struct AssignExpr : Expression { std::string op; std::shared_ptr<Expression> target, value; };
struct TernaryExpr: Expression { std::shared_ptr<Expression> cond, then_e, else_e; };
struct CallExpr   : Expression {
    std::shared_ptr<Expression> receiver;     // may be null
    std::string method;
    std::vector<std::shared_ptr<Expression>> args;
    std::shared_ptr<Statement> block;          // do/end or {...} body, may be null
    std::vector<std::string> block_params;
};
struct IndexExpr  : Expression { std::shared_ptr<Expression> obj; std::vector<std::shared_ptr<Expression>> idx; };
struct MemberExpr : Expression { std::shared_ptr<Expression> obj; std::string member; bool safe{false}; };
struct RangeExpr  : Expression { std::shared_ptr<Expression> from, to; bool exclusive{false}; };

// ---- Statements ------------------------------------------------------------

struct ExprStmt   : Statement { std::shared_ptr<Expression> expr; };
struct Block      : Statement { std::vector<std::shared_ptr<Statement>> stmts; };
struct ReturnStmt : Statement { std::shared_ptr<Expression> value; };
struct YieldStmt  : Statement { std::vector<std::shared_ptr<Expression>> args; };
struct IfStmt     : Statement {
    std::shared_ptr<Expression> cond;
    std::shared_ptr<Statement>  then_branch;
    std::shared_ptr<Statement>  else_branch;
    bool unless{false};
};
struct WhileStmt  : Statement {
    std::shared_ptr<Expression> cond;
    std::shared_ptr<Statement>  body;
    bool until{false};
};
struct ForStmt    : Statement {
    std::string var;
    std::shared_ptr<Expression> iterable;
    std::shared_ptr<Statement>  body;
};
struct CaseStmt   : Statement {
    struct When { std::vector<std::shared_ptr<Expression>> tests; std::shared_ptr<Statement> body; };
    std::shared_ptr<Expression> subject;
    std::vector<When> whens;
    std::shared_ptr<Statement> else_branch;
};
struct BeginStmt  : Statement {
    std::shared_ptr<Statement> body;
    struct Rescue { std::vector<std::shared_ptr<TypeNode>> classes; std::string var; std::shared_ptr<Statement> body; };
    std::vector<Rescue> rescues;
    std::shared_ptr<Statement> else_branch;
    std::shared_ptr<Statement> ensure_branch;
};
struct BreakStmt    : Statement { std::shared_ptr<Expression> value; };
struct NextStmt     : Statement { std::shared_ptr<Expression> value; };
struct RedoStmt     : Statement {};
struct RetryStmt    : Statement {};

// ---- Declarations ----------------------------------------------------------

struct Param { std::string name; std::shared_ptr<TypeNode> type; std::shared_ptr<Expression> default_value; bool splat{false}; bool double_splat{false}; bool block{false}; };

struct MethodDecl : Statement {
    std::string name;
    std::shared_ptr<Expression> receiver;     // for `def self.name(...)`
    std::vector<Param> params;
    std::shared_ptr<TypeNode> return_type;
    std::shared_ptr<Statement> body;
    bool is_self{false};
};

struct ClassDecl : Statement {
    std::string name;
    std::shared_ptr<Expression> superclass;
    std::vector<std::shared_ptr<Statement>> body;
};

struct ModuleDecl : Statement {
    std::string name;
    std::vector<std::shared_ptr<Statement>> body;
};

// ---- Module ----------------------------------------------------------------

struct Module {
    std::string filename;
    std::vector<std::shared_ptr<Statement>> body;
};

}  // namespace polyglot::ruby
