/**
 * @file     ploy_lexer.h
 * @brief    Ploy language frontend
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <vector>

#include "frontends/common/include/lexer_base.h"

namespace polyglot::ploy {

/** @brief PloyLexer class. */
class PloyLexer : public frontends::LexerBase {
public:
  PloyLexer(std::string source, std::string file) : LexerBase(std::move(source), std::move(file)) {}

  frontends::Token NextToken() override;

  // Documentation comments (since v1.18.0).  `///`-prefixed line comments
  // are accumulated by the lexer as raw text (one entry per `///` line)
  // and exposed to the parser via `TakePendingDoc()` so it can attach the
  // block to the immediately following top-level declaration.  Calling
  // `TakePendingDoc()` clears the buffer.  Any non-comment, non-whitespace
  // token also clears the buffer if the parser does not collect it first.
  std::vector<std::string> TakePendingDoc() {
    std::vector<std::string> out;
    out.swap(pending_doc_);
    return out;
  }

private:
  void SkipWhitespace();
  void SkipLineComment();
  void SkipDocLineComment();
  void SkipBlockComment();
  frontends::Token LexIdentifierOrKeyword();
  frontends::Token LexNumber();
  frontends::Token LexString();
  // Extended string literal forms (since v1.17.0).  Each helper consumes the
  // entire source span (including the prefix / opening quote) and returns a
  // `kString` token whose lexeme is re-encoded into the canonical
  // `"..."` (or `f"..."` / `r"..."` / `r#"..."#` / `"""..."""`) form so
  // the parser can dispatch on the leading character without re-running the
  // lexer's escape-handling rules.
  frontends::Token LexRawString();        // r"..."  /  r#"..."#  /  r##"..."##
  frontends::Token LexMultilineString();  // """..."""
  frontends::Token LexTemplateString();   // f"..."  (incl. f"""...""" multiline)
  frontends::Token LexOperator();

  // Buffered `///` doc-comment lines awaiting attachment by the parser.
  std::vector<std::string> pending_doc_;
};

} // namespace polyglot::ploy
