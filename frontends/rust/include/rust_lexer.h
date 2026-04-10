/**
 * @file     rust_lexer.h
 * @brief    Rust language frontend
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/lexer_base.h"

namespace polyglot::rust {

/** @brief RustLexer class. */
class RustLexer : public frontends::LexerBase {
  public:
    RustLexer(std::string source, std::string file)
        : LexerBase(std::move(source), std::move(file)) {}

    frontends::Token NextToken() override;

  private:
    void SkipWhitespace();
    void SkipLineComment();
    void SkipBlockComment();
    frontends::Token LexIdentifierOrKeyword();
    frontends::Token LexRawIdentifier();
    frontends::Token LexNumber();
    frontends::Token LexString();
    frontends::Token LexChar();
    frontends::Token LexOperator();
};

} // namespace polyglot::rust
