#pragma once

#include "frontends/common/include/lexer_base.h"

namespace polyglot::python {

class PythonLexer : public frontends::LexerBase {
 public:
  PythonLexer(std::string source, std::string file)
      : LexerBase(std::move(source), std::move(file)) {}

 frontends::Token NextToken() override;

 private:
  void SkipWhitespace();
  void SkipComment();
  frontends::Token LexIdentifier();
  frontends::Token LexNumber();
  frontends::Token LexString();
};

}  // namespace polyglot::python
