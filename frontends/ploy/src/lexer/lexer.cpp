/**
 * @file     lexer.cpp
 * @brief    Ploy language frontend implementation
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <cctype>
#include <unordered_set>

#include "frontends/ploy/include/ploy_lexer.h"

namespace polyglot::ploy {

static bool IsIdentStart(unsigned char c) {
  return std::isalpha(c) || c == '_';
}

static bool IsIdentContinue(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

void PloyLexer::SkipWhitespace() {
  while (!Eof()) {
    unsigned char c = static_cast<unsigned char>(Peek());
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      Get();
    } else {
      break;
    }
  }
}

void PloyLexer::SkipLineComment() {
  // Skip until end of line
  while (!Eof() && Peek() != '\n') {
    Get();
  }
}

void PloyLexer::SkipBlockComment() {
  // Already consumed '/*', now find '*/'
  while (!Eof()) {
    if (Peek() == '*' && PeekNext() == '/') {
      Get(); // consume '*'
      Get(); // consume '/'
      return;
    }
    Get();
  }
  // Unterminated block comment — the parser/diagnostics will handle this
}

frontends::Token PloyLexer::LexIdentifierOrKeyword() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (!Eof() && IsIdentContinue(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }

  // Canonical (upper-case) spellings of every reserved word.  Keyword matching
  // is *case-insensitive* by language design: `LINK`, `link`, `Link` and
  // `LiNk` all map to the canonical `LINK` keyword.  Identifiers, on the
  // other hand, remain case-sensitive (handled by the fall-through path
  // below).  Any addition / removal here must be mirrored in
  // docs/specs/language_spec_{zh,en}.md and in the parser's Sync() set.
  static const std::unordered_set<std::string> kCanonicalKeywords = {
      "LINK",     "IMPORT",  "EXPORT", "MAP_TYPE", "PIPELINE", "FUNC",   "LET",    "VAR",
      "RETURN",   "RETURNS", "IF",     "ELSE",     "WHILE",    "FOR",    "IN",     "MATCH",
      "CASE",     "DEFAULT", "BREAK",  "CONTINUE", "AS",       "TRUE",   "FALSE",  "NULL",
      "AND",      "OR",      "NOT",    "CALL",     "VOID",     "INT",    "FLOAT",  "STRING",
      "BOOL",     "ARRAY",   "STRUCT", "PACKAGE",  "LIST",     "TUPLE",  "DICT",   "OPTION",
      "MAP_FUNC", "CONVERT", "CONFIG", "VENV",     "CONDA",    "UV",     "PIPENV", "POETRY",
      // language-version pinning keyword.
      "NEW",      "METHOD",  "GET",    "SET",      "WITH",     "DELETE", "EXTEND", "LANG",
  // runtime-IO statement: writes a literal message to standard output.
  "PRINTLN",
  // Pipeline stage marker keyword (demand 2026-04-28-8).
  // STAGE is reserved and treated as a keyword so IDEs can provide
  // consistent highlighting inside PIPELINE blocks.  Parser enforces
  // that STAGE is only valid within PIPELINE bodies.
  "STAGE",
      // Explicit-width primitive type keywords (demand 2026-04-28-7).
      // These canonical entries are upper-case because the lexer folds the
      // source spelling to upper-case before lookup; user code typically
      // writes them lower-case (`i32`, `u64`, `f32`) per the recommended
      // style, but `I32`, `U64`, `F32` are equally accepted.
      "I8",  "I16",  "I32",  "I64", "U8",  "U16", "U32", "U64",
      "F32", "F64",  "USIZE","ISIZE",
      // Type-system declaration keywords (demand 2026-04-28-7):
      //   TYPE  <name> = <type_expr>;          -- type alias
      //   CONST <name>: <type> = <const_expr>; -- compile-time constant
      "TYPE", "CONST"};

      // NOTE — Static-typed cross-language object interop tokens
      // (CLASS / HANDLE / ATTR, demand 2026-04-28-9) are intentionally
      // *NOT* listed above.  They are handled as **contextual keywords**
      // by the parser: an ordinary identifier with that exact (case-
      // insensitive) spelling is recognised in the appropriate position
      // (top-level declaration, type expression, CLASS body row).  Adding
      // them to the global keyword set was rejected because common
      // English words like `handle` are widely used as variable names in
      // existing samples and would otherwise be silently shadowed.

  // ASCII upper-case fold.  Reserved words are intentionally restricted to
  // the ASCII range, so a per-byte fold is correct without a Unicode pass.
  std::string folded;
  folded.reserve(lexeme.size());
  for (char ch : lexeme) {
    folded.push_back(static_cast<char>(
        std::toupper(static_cast<unsigned char>(ch))));
  }

  if (kCanonicalKeywords.count(folded) != 0) {
    frontends::Token tok{};
    tok.kind = frontends::TokenKind::kKeyword;
    tok.lexeme = folded;            // canonical (upper-case) form for parser comparison
    tok.loc = loc;
    if (folded != lexeme) {
      tok.raw_lexeme = lexeme;       // preserve source spelling when case differs
    }
    return tok;
  }

  return frontends::Token{frontends::TokenKind::kIdentifier, lexeme, loc};
}

frontends::Token PloyLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  bool is_float = false;

  // Handle hex, binary, octal prefixes
  if (Peek() == '0' && !Eof()) {
    lexeme.push_back(Get());
    if (Peek() == 'x' || Peek() == 'X') {
      lexeme.push_back(Get());
      while (!Eof() && std::isxdigit(static_cast<unsigned char>(Peek()))) {
        lexeme.push_back(Get());
      }
      return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
    }
    if (Peek() == 'b' || Peek() == 'B') {
      lexeme.push_back(Get());
      while (!Eof() && (Peek() == '0' || Peek() == '1')) {
        lexeme.push_back(Get());
      }
      return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
    }
    if (Peek() == 'o' || Peek() == 'O') {
      lexeme.push_back(Get());
      while (!Eof() && Peek() >= '0' && Peek() <= '7') {
        lexeme.push_back(Get());
      }
      return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
    }
  }

  // Decimal digits
  while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }

  // Fractional part
  if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(PeekNext()))) {
    is_float = true;
    lexeme.push_back(Get()); // consume '.'
    while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek()))) {
      lexeme.push_back(Get());
    }
  }

  // Exponent
  if (Peek() == 'e' || Peek() == 'E') {
    is_float = true;
    lexeme.push_back(Get());
    if (Peek() == '+' || Peek() == '-') {
      lexeme.push_back(Get());
    }
    while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek()))) {
      lexeme.push_back(Get());
    }
  }

  (void)is_float; // Token kind is always kNumber; consumers differentiate
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token PloyLexer::LexString() {
  core::SourceLoc loc = CurrentLoc();
  char quote = Get(); // consume opening '"'
  std::string lexeme;
  lexeme.push_back(quote);

  while (!Eof() && Peek() != quote) {
    if (Peek() == '\\') {
      lexeme.push_back(Get()); // consume backslash
      if (!Eof()) {
        lexeme.push_back(Get()); // consume escaped character
      }
    } else {
      lexeme.push_back(Get());
    }
  }

  if (!Eof()) {
    lexeme.push_back(Get()); // consume closing quote
  }

  return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token PloyLexer::LexOperator() {
  core::SourceLoc loc = CurrentLoc();
  char c = Get();
  std::string lexeme(1, c);

  switch (c) {
  case '(':
  case ')':
  case '{':
  case '}':
  case '[':
  case ']':
  case ',':
  case ';':
  case '%':
    break;

  case ':':
    if (Peek() == ':') {
      lexeme.push_back(Get());
    }
    break;

  case '-':
    if (Peek() == '>') {
      lexeme.push_back(Get());
    }
    break;

  case '=':
    if (Peek() == '=') {
      lexeme.push_back(Get());
    }
    break;

  case '!':
    if (Peek() == '=') {
      lexeme.push_back(Get());
    }
    break;

  case '<':
    if (Peek() == '=') {
      lexeme.push_back(Get());
    }
    break;

  case '>':
    if (Peek() == '=') {
      lexeme.push_back(Get());
    }
    break;

  case '&':
    if (Peek() == '&') {
      lexeme.push_back(Get());
    }
    break;

  case '|':
    if (Peek() == '|') {
      lexeme.push_back(Get());
    }
    break;

  case '+':
  case '*':
    break;

  case '~':
    if (Peek() == '=') {
      lexeme.push_back(Get());
    }
    break;

  case '.':
    if (Peek() == '.') {
      lexeme.push_back(Get());
      // Inclusive-range suffix `..=` is a distinct symbol used by patterns
      // and range expressions.  Emit the three characters as a single token
      // so downstream consumers do not have to peek past `..`.
      if (Peek() == '=') {
        lexeme.push_back(Get());
      }
    }
    break;

  default:
    break;
  }

  return frontends::Token{frontends::TokenKind::kSymbol, lexeme, loc};
}

frontends::Token PloyLexer::NextToken() {
  // Skip whitespace and comments
  while (!Eof()) {
    SkipWhitespace();
    if (Eof())
      break;

    // Handle comments
    if (Peek() == '/' && PeekNext() == '/') {
      Get();
      Get(); // consume '//'
      SkipLineComment();
      continue;
    }
    if (Peek() == '/' && PeekNext() == '*') {
      Get();
      Get(); // consume '/*'
      SkipBlockComment();
      continue;
    }

    break;
  }

  if (Eof()) {
    return frontends::Token{frontends::TokenKind::kEndOfFile, "", CurrentLoc()};
  }

  unsigned char c = static_cast<unsigned char>(Peek());

  // Identifiers and keywords
  if (IsIdentStart(c)) {
    return LexIdentifierOrKeyword();
  }

  // Numbers
  if (std::isdigit(c)) {
    return LexNumber();
  }

  // Strings
  if (c == '"') {
    return LexString();
  }

  // Division operator (not a comment — already handled above)
  if (c == '/') {
    core::SourceLoc loc = CurrentLoc();
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, "/", loc};
  }

  // Operators and punctuation
  return LexOperator();
}

} // namespace polyglot::ploy
