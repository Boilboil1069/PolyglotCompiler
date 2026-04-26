/**
 * @file     ruby_lexer.h
 * @brief    Ruby tokenizer (2.7+ / 3.x)
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <string>

#include "frontends/common/include/lexer_base.h"

namespace polyglot::ruby {

class RbLexer : public frontends::LexerBase {
  public:
    RbLexer(std::string source, std::string file)
        : LexerBase(std::move(source), std::move(file)) {}

    frontends::Token NextToken() override;

    // Drained by the parser before parsing a `def`/`class`/`module` so that
    // YARD comments (@param/@return tags) immediately above can be inspected.
    std::string TakeDocComment() {
        std::string s = std::move(pending_doc_); pending_doc_.clear(); return s;
    }

  private:
    void SkipSpacesAndContinuations();
    void SkipLineComment(bool *is_yard);
    frontends::Token LexIdentifierOrKeyword();
    frontends::Token LexNumber();
    frontends::Token LexString(char quote);
    frontends::Token LexSymbol();
    frontends::Token LexHeredoc(const std::string &tag, bool indent_strip);
    frontends::Token LexOperator();
    bool AtLineStart() const;

    std::string pending_doc_;
    bool prev_allows_unary_{true};   // distinguishes `- x` (unary) from `a - x`
};

}  // namespace polyglot::ruby
