/**
 * @file     javascript_lexer.h
 * @brief    JavaScript (ES2020+) lexer
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <string>
#include <vector>

#include "frontends/common/include/lexer_base.h"

namespace polyglot::javascript {

/** @brief JavaScript lexer. */
class JsLexer : public frontends::LexerBase {
  public:
    JsLexer(std::string source, std::string file)
        : LexerBase(std::move(source), std::move(file)) {}

    frontends::Token NextToken() override;

    // Track the most recently emitted significant token so a subsequent '/'
    // can be classified as either a divide operator or a regex literal.
    bool LastTokenAllowsRegex() const { return prev_allows_regex_; }

    // Doc comments accumulated immediately above the current cursor.
    // Consumed by the parser when it encounters a function/class/method.
    std::string TakeDocComment() {
        std::string out = std::move(pending_doc_);
        pending_doc_.clear();
        return out;
    }

  private:
    void SkipWhitespace();
    void SkipLineComment();
    void SkipBlockComment(bool *had_doc);
    frontends::Token LexIdentifierOrKeyword();
    frontends::Token LexNumber();
    frontends::Token LexString(char quote);
    frontends::Token LexTemplateString();
    frontends::Token LexRegex();
    frontends::Token LexOperator();

    bool prev_allows_regex_{true};
    std::string pending_doc_;
};

}  // namespace polyglot::javascript
