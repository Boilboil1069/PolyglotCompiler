#pragma once

#include "frontends/common/include/lexer_base.h"

namespace polyglot::cpp {

class CppLexer : public frontends::LexerBase {
 public:
  CppLexer(std::string source, std::string file)
      : LexerBase(std::move(source), std::move(file)) {}

  frontends::Token NextToken() override;

 private:
  void SkipWhitespace();
  void SkipLineComment();
  frontends::Token LexIdentifierOrKeyword();
  frontends::Token LexNumber();
};

}  // namespace polyglot::cpp
