/**
 * @file     python_lexer.h
 * @brief    Python language frontend
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/lexer_base.h"

#include <vector>

namespace polyglot::python {

/** @brief PythonLexer class. */
class PythonLexer : public frontends::LexerBase {
  public:
    PythonLexer(std::string source, std::string file)
        : LexerBase(std::move(source), std::move(file)) {}
    PythonLexer(std::string source, std::string file, frontends::Diagnostics *diagnostics)
        : LexerBase(std::move(source), std::move(file)), diagnostics_(diagnostics) {}

    frontends::Token NextToken() override;

  private:
    void SkipWhitespace();
    void SkipComment();
    frontends::Token LexIdentifier();
    frontends::Token LexNumber();
    frontends::Token LexString();
    frontends::Token LexStringInternal(bool allow_formatting);
    frontends::Token LexOperator();
    void HandleIndentation();
    bool ParseFormatExpression(core::SourceLoc brace_loc);
    void Report(const core::SourceLoc &loc, const std::string &message);

    std::vector<frontends::Token> pending_{};
    std::vector<int> indent_stack_{0};
    int paren_level_{0};
    bool at_line_start_{true};
    frontends::Diagnostics *diagnostics_{nullptr};
};

} // namespace polyglot::python
