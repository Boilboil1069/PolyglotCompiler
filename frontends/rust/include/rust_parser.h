#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_lexer.h"

namespace polyglot::rust {

class RustParser : public frontends::ParserBase {
 public:
  RustParser(RustLexer &lexer, frontends::Diagnostics &diagnostics)
      : ParserBase(diagnostics), lexer_(lexer) {}

  void ParseModule() override;
  std::shared_ptr<Module> TakeModule();

 private:
  frontends::Token Consume();
  bool IsSymbol(const std::string &symbol) const;
  bool MatchSymbol(const std::string &symbol);
  bool MatchKeyword(const std::string &keyword);
  void ExpectSymbol(const std::string &symbol, const std::string &message);
  void Sync();
  void ParseItem();
  std::shared_ptr<Statement> ParseStatement();
  std::shared_ptr<Statement> ParseUse();
  std::shared_ptr<Statement> ParseLet();
  std::shared_ptr<Statement> ParseFunction();
  std::shared_ptr<Statement> ParseReturn();
  std::shared_ptr<Expression> ParseIfExpression();
  std::shared_ptr<Expression> ParseWhileExpression();
  std::shared_ptr<BlockExpression> ParseBlockExpression();
  std::shared_ptr<Expression> ParseExpression();
  std::shared_ptr<Expression> ParsePostfix();
  std::shared_ptr<Expression> ParseUnary();
  std::shared_ptr<Expression> ParseBinaryExpression(int min_prec = 1);
  std::shared_ptr<Expression> ParsePrimary();
  int GetPrecedence() const;

  RustLexer &lexer_;
  std::shared_ptr<Module> module_{std::make_shared<Module>()};
  frontends::Token current_{};
};

}  // namespace polyglot::rust
