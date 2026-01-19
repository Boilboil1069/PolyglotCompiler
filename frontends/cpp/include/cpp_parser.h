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
  void ParseTopLevel();

  CppLexer &lexer_;
  std::shared_ptr<Module> module_{std::make_shared<Module>()};
  frontends::Token current_{};
};

}  // namespace polyglot::cpp
