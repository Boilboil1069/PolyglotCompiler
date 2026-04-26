/**
 * @file     ruby_parser.h
 * @brief    Ruby parser
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/ruby/include/ruby_ast.h"
#include "frontends/ruby/include/ruby_lexer.h"

namespace polyglot::ruby {

class RbParser : public frontends::ParserBase {
  public:
    RbParser(RbLexer &lexer, frontends::Diagnostics &d)
        : ParserBase(d), lexer_(lexer) {}

    void ParseModule() override;
    std::shared_ptr<Module> TakeModule();

  private:
    void Advance();
    bool IsKeyword(const std::string &k) const;
    bool IsSymbol(const std::string &s) const;
    bool MatchKeyword(const std::string &k);
    bool MatchSymbol(const std::string &s);
    bool ExpectKeyword(const std::string &k, const std::string &msg);
    bool ExpectSymbol(const std::string &s, const std::string &msg);
    void SkipTerminators();
    bool AtTerminator() const;

    std::shared_ptr<Statement> ParseTopLevel();
    std::shared_ptr<Statement> ParseStatement();
    std::shared_ptr<Statement> ParseDef();
    std::shared_ptr<Statement> ParseClass();
    std::shared_ptr<Statement> ParseModuleStmt();
    std::shared_ptr<Statement> ParseIf(bool is_unless);
    std::shared_ptr<Statement> ParseWhile(bool is_until);
    std::shared_ptr<Statement> ParseFor();
    std::shared_ptr<Statement> ParseCase();
    std::shared_ptr<Statement> ParseBegin();
    std::shared_ptr<Block>     ParseBlockUntil(std::initializer_list<std::string> terminators);
    std::vector<Param>         ParseDefParams();
    std::shared_ptr<TypeNode>  ParseYardType(const std::string &raw);

    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseAssignment();
    std::shared_ptr<Expression> ParseTernary();
    std::shared_ptr<Expression> ParseRange();
    std::shared_ptr<Expression> ParseOrExpr();
    std::shared_ptr<Expression> ParseAndExpr();
    std::shared_ptr<Expression> ParseNotExpr();
    std::shared_ptr<Expression> ParseDefined();
    std::shared_ptr<Expression> ParseEquality();
    std::shared_ptr<Expression> ParseComparison();
    std::shared_ptr<Expression> ParseBitOr();
    std::shared_ptr<Expression> ParseBitAnd();
    std::shared_ptr<Expression> ParseShift();
    std::shared_ptr<Expression> ParseAdditive();
    std::shared_ptr<Expression> ParseMultiplicative();
    std::shared_ptr<Expression> ParseUnary();
    std::shared_ptr<Expression> ParsePower();
    std::shared_ptr<Expression> ParsePostfix();
    std::shared_ptr<Expression> ParsePrimary();
    std::shared_ptr<Expression> ParseCallTail(std::shared_ptr<Expression> e);
    std::shared_ptr<Expression> ParseArray();
    std::shared_ptr<Expression> ParseHash();
    std::vector<std::shared_ptr<Expression>> ParseCallArgs(bool until_paren);

    RbLexer &lexer_;
    frontends::Token current_{};
    std::shared_ptr<Module> module_;
    std::string pending_doc_;
};

}  // namespace polyglot::ruby
