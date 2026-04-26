/**
 * @file     go_lexer.h
 * @brief    Go language lexer
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "frontends/common/include/lexer_base.h"

namespace polyglot::go {

class GoLexer : public frontends::LexerBase {
  public:
    GoLexer(const std::string &src, const std::string &file)
        : frontends::LexerBase(src, file) {}

    frontends::Token NextToken() override;

    /// Doc comment captured immediately above the next significant token.
    std::string TakePendingDoc() { auto d = std::move(pending_doc_); pending_doc_.clear(); return d; }

    /// Whether the lexer should emit an automatic semicolon after the last
    /// token, mirroring Go's actual lexer rule (semicolon insertion at EOL).
    bool prev_was_terminator() const { return prev_terminator_; }

  private:
    void SkipWhitespaceAndComments();
    frontends::Token LexIdentifierOrKeyword();
    frontends::Token LexNumber();
    frontends::Token LexString(char quote);
    frontends::Token LexRawString();
    frontends::Token LexRune();
    frontends::Token LexOperator();

    std::string pending_doc_;
    bool prev_terminator_{false};
    bool emit_semi_{false};        // pending automatic semicolon
};

}  // namespace polyglot::go
