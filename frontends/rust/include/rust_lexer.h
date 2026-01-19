#pragma once

#include "frontends/common/include/lexer_base.h"

namespace polyglot::rust {

class RustLexer : public frontends::LexerBase {
 public:
  RustLexer(std::string source, std::string file)
      : LexerBase(std::move(source), std::move(file)) {}

  frontends::Token NextToken() override;

 private:
  void SkipWhitespace();
  frontends::Token LexIdentifierOrKeyword();
  frontends::Token LexNumber();
};

}  // namespace polyglot::rust
