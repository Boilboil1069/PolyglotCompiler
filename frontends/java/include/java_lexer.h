#pragma once

#include "frontends/common/include/lexer_base.h"

#include <vector>

namespace polyglot::java {

class JavaLexer : public frontends::LexerBase {
  public:
    JavaLexer(std::string source, std::string file)
        : LexerBase(std::move(source), std::move(file)) {}

    frontends::Token NextToken() override;

  private:
    void SkipWhitespace();
    void SkipLineComment();
    void SkipBlockComment();
    frontends::Token LexIdentifierOrKeyword();
    frontends::Token LexNumber();
    frontends::Token LexString();
    frontends::Token LexChar();
    frontends::Token LexTextBlock();
    frontends::Token LexOperator();
    frontends::Token LexAnnotation();

    std::vector<frontends::Token> pending_{};
};

} // namespace polyglot::java
