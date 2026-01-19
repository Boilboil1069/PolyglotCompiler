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
  bool MatchSymbol(const std::string &symbol);
  bool MatchKeyword(const std::string &keyword);
  void ExpectSymbol(const std::string &symbol, const std::string &message);
  void Sync();
  void ParseTopLevel();
  std::shared_ptr<Statement> ParseStatement();
  std::shared_ptr<Statement> ParseImport();
  std::shared_ptr<Statement> ParseFunction();
  std::shared_ptr<Statement> ParseReturn();
  std::shared_ptr<Expression> ParseExpression();
  std::shared_ptr<Expression> ParsePrimary();

  PythonLexer &lexer_;
  std::shared_ptr<Module> module_{std::make_shared<Module>()};
  frontends::Token current_{};
};

}  // namespace polyglot::python
