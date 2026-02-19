#pragma once

#include "frontends/common/include/lexer_base.h"

#include <vector>

namespace polyglot::ploy {

class PloyLexer : public frontends::LexerBase {
  public:
    PloyLexer(std::string source, std::string file)
        : LexerBase(std::move(source), std::move(file)) {}

    frontends::Token NextToken() override;

  private:
    void SkipWhitespace();
    void SkipLineComment();
    void SkipBlockComment();
    frontends::Token LexIdentifierOrKeyword();
    frontends::Token LexNumber();
    frontends::Token LexString();
    frontends::Token LexOperator();
};

} // namespace polyglot::ploy
