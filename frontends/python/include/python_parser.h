#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/python/include/python_ast.h"
#include "frontends/python/include/python_lexer.h"

namespace polyglot::python {

class PythonParser : public frontends::ParserBase {
  public:
    PythonParser(PythonLexer &lexer, frontends::Diagnostics &diagnostics)
        : ParserBase(diagnostics), lexer_(lexer) {}

    void ParseModule() override;

    std::shared_ptr<Module> TakeModule();

  private:
    frontends::Token Consume();
    void Advance();
    void SkipNewlines();
    bool IsSymbol(const std::string &symbol) const;
    bool MatchSymbol(const std::string &symbol);
    bool MatchKeyword(const std::string &keyword);
    void ExpectSymbol(const std::string &symbol, const std::string &message);
    void Sync();
    void ParseTopLevel();
    std::shared_ptr<Statement> ParseStatement();
    std::vector<std::shared_ptr<Statement>> ParseSuite();
    std::shared_ptr<Statement> ParseImport();
    std::shared_ptr<Statement> ParseClass();
    std::shared_ptr<Statement> ParseFunction();
    std::shared_ptr<Statement> ParseReturn();
    std::shared_ptr<Statement> ParseIf();
    std::shared_ptr<Statement> ParseWhile();
    std::shared_ptr<Statement> ParseFor();
    std::shared_ptr<Expression> ParseExpression();
    std::shared_ptr<Expression> ParseLambda();
    std::shared_ptr<Expression> ParseOr();
    std::shared_ptr<Expression> ParseAnd();
    std::shared_ptr<Expression> ParseNot();
    std::shared_ptr<Expression> ParseComparison();
    std::shared_ptr<Expression> ParseAdditive();
    std::shared_ptr<Expression> ParseMultiplicative();
    std::shared_ptr<Expression> ParseUnary();
    std::shared_ptr<Expression> ParsePower();
    std::shared_ptr<Expression> ParsePostfix();
    std::shared_ptr<Expression> ParsePrimary();

    PythonLexer &lexer_;
    std::shared_ptr<Module> module_{std::make_shared<Module>()};
    frontends::Token current_{};
};

} // namespace polyglot::python
