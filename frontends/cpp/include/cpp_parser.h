#pragma once

#include <memory>

#include "frontends/common/include/parser_base.h"
#include "frontends/cpp/include/cpp_ast.h"
#include "frontends/cpp/include/cpp_lexer.h"

namespace polyglot::cpp {

class CppParser : public frontends::ParserBase {
 public:
  CppParser(CppLexer &lexer, frontends::Diagnostics &diagnostics)
      : ParserBase(diagnostics), lexer_(lexer) {}

  void ParseModule() override;
  std::shared_ptr<Module> TakeModule();

 private:
  frontends::Token Consume();
  void Advance();
  bool IsSymbol(const std::string &symbol) const;
  bool MatchSymbol(const std::string &symbol);
  bool MatchKeyword(const std::string &keyword);
  void ExpectSymbol(const std::string &symbol, const std::string &message);
  void Sync();
  void ParseTopLevel();
  std::shared_ptr<Statement> ParseStatement();
  std::shared_ptr<Statement> ParseBlock();
  std::shared_ptr<Statement> ParseIf();
  std::shared_ptr<Statement> ParseWhile();
  std::shared_ptr<Statement> ParseFor();
  std::shared_ptr<Statement> ParseImport();
  std::shared_ptr<Statement> ParseUsing();
  std::shared_ptr<Statement> ParseFunction();
  std::shared_ptr<Statement> ParseFunctionWithSignature(std::shared_ptr<TypeNode> ret_type,
                                                        const std::string &name);
  std::shared_ptr<Statement> ParseVarDecl(std::shared_ptr<TypeNode> type,
                                          const std::string &name);
  std::shared_ptr<Statement> ParseRecord(const std::string &kind);
  std::shared_ptr<Statement> ParseEnum();
  std::shared_ptr<Statement> ParseNamespace();
  std::shared_ptr<Statement> ParseTemplate();
  std::shared_ptr<Statement> ParseReturn();
  std::shared_ptr<TypeNode> ParseType();
  std::shared_ptr<Expression> ParseExpression();
  std::shared_ptr<Expression> ParseAssignment();
  std::shared_ptr<Expression> ParseLogicalOr();
  std::shared_ptr<Expression> ParseLogicalAnd();
  std::shared_ptr<Expression> ParseEquality();
  std::shared_ptr<Expression> ParseRelational();
  std::shared_ptr<Expression> ParseAdditive();
  std::shared_ptr<Expression> ParseMultiplicative();
  std::shared_ptr<Expression> ParseUnary();
  std::shared_ptr<Expression> ParsePostfix();
  std::shared_ptr<Expression> ParsePrimary();
  std::shared_ptr<Expression> ParseLambda();
  std::vector<std::string> ParseTemplateParams();

  CppLexer &lexer_;
  std::shared_ptr<Module> module_{std::make_shared<Module>()};
  frontends::Token current_{};
};

}  // namespace polyglot::cpp
