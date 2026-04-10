/**
 * @file     cpp_lexer.h
 * @brief    C++ language frontend
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/lexer_base.h"

#include <vector>

namespace polyglot::cpp {

/** @brief CppLexer class. */
class CppLexer : public frontends::LexerBase {
  public:
    CppLexer(std::string source, std::string file)
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
    frontends::Token LexPreprocessor();
    frontends::Token LexOperator();
    bool StartsWithStringPrefix() const;

    std::vector<frontends::Token> pending_{};
};

} // namespace polyglot::cpp
