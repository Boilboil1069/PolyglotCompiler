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

void PloyLexer::SkipDocLineComment() {
  // The leading `///` has already been consumed.  An optional single
  // leading space is stripped so authors can write `/// hello` and have
  // the rendered doc read "hello" rather than " hello".  Any trailing
  // CR is dropped so cross-platform sources collapse to LF in the
  // accumulated buffer.
  if (Peek() == ' ') Get();
  std::string line;
  while (!Eof() && Peek() != '\n') {
    char c = Get();
    if (c != '\r') line.push_back(c);
  }
  pending_doc_.push_back(std::move(line));
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
      "TYPE", "CONST",
      // Structured exception handling keywords (since v1.13.0):
      //   TRY { … } CATCH (e: Error) { … } FINALLY { … }
      //   THROW <expr>;
      // These are reserved unconditionally so they cannot be shadowed by
      // user identifiers; matching is case-insensitive like every other
      // keyword.  `ERROR` is the canonical spelling of the built-in
      // exception handle type and is also reserved here so source files
      // may write it as a contextual type name without quoting.
      "TRY", "CATCH", "FINALLY", "THROW", "ERROR",
      // Async / await keywords (since v1.14.0):
      //   ASYNC FUNC name(...) -> T { ... }
      //   LET v = AWAIT call();
      // Both are reserved unconditionally so source files cannot redefine
      // them as ordinary identifiers.  Matching follows the same case-
      // insensitive rule as every other keyword.
      "ASYNC", "AWAIT",
      // Generics keywords (since v1.15.0):
      //   FUNC max<T: Comparable>(a: T, b: T) -> T { ... }
      //   FUNC f<T>(x: T) WHERE T: Numeric { ... }
      // Only `WHERE` needs to be reserved at the lexical level; the angle
      // brackets used for type parameters reuse the generic `<` / `>`
      // symbols and are disambiguated in type-position by the parser.
      "WHERE",
      // Visibility modifiers (since v1.16.0): PUB exports across module
      // boundaries; PRIVATE is the default and may be written explicitly
      // for documentation.  Both are reserved unconditionally.
      "PUB", "PRIVATE"};

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
  // Triple-quoted multiline form: """...""" with literal newlines preserved.
  // We dispatch here (rather than inside NextToken) so the leading-quote check
  // already established that Peek() == '"'.
  if (Peek() == '"' && PeekNext() == '"') {
    // Tentative: this is either a triple-quoted string or an empty literal.
    // Look one further character ahead to distinguish.
    char first = Get();          // first '"'
    char second = Get();         // second '"'
    if (Peek() == '"') {
      Get();                     // consume third '"'
      // Re-enter via the multiline helper, but we have already eaten the
      // opening triple, so we hand-roll the body here for symmetry with
      // LexMultilineString's contract.
      std::string body;
      while (!Eof()) {
        if (Peek() == '"' && PeekNext() == '"') {
          // Look for closing triple.
          char saved_pos_q1 = Get();
          char saved_pos_q2 = Get();
          if (Peek() == '"') {
            Get();
            // Re-encode the body into a canonical "..." form so downstream
            // (parser / lowering / PRINTLN decoder) keeps working without
            // change.  Newlines become \n, CR becomes \r, tab becomes \t,
            // backslash becomes \\, double-quote becomes \".
            std::string canonical;
            canonical.reserve(body.size() + 2);
            canonical.push_back('"');
            for (char ch : body) {
              switch (ch) {
              case '\n': canonical += "\\n"; break;
              case '\r': canonical += "\\r"; break;
              case '\t': canonical += "\\t"; break;
              case '\\': canonical += "\\\\"; break;
              case '"':  canonical += "\\\""; break;
              default:   canonical.push_back(ch); break;
              }
            }
            canonical.push_back('"');
            return frontends::Token{frontends::TokenKind::kString, canonical, loc};
          }
          body.push_back(saved_pos_q1);
          body.push_back(saved_pos_q2);
          continue;
        }
        body.push_back(Get());
      }
      // Unterminated triple-quoted string — fall through with what we have.
      std::string canonical;
      canonical.push_back('"');
      for (char ch : body) {
        switch (ch) {
        case '\n': canonical += "\\n"; break;
        case '\r': canonical += "\\r"; break;
        case '\t': canonical += "\\t"; break;
        case '\\': canonical += "\\\\"; break;
        case '"':  canonical += "\\\""; break;
        default:   canonical.push_back(ch); break;
        }
      }
      canonical.push_back('"');
      return frontends::Token{frontends::TokenKind::kString, canonical, loc};
    }
    // Not a triple — it was an empty `""` literal.  Reconstruct.
    std::string lexeme;
    lexeme.push_back(first);
    lexeme.push_back(second);
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
  }

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

// Raw string literal (since v1.17.0): r"..."  /  r#"..."#  /  r##"..."##
//
// Inside a raw string no backslash escape is recognised — the bytes between
// the opening and closing delimiter are taken verbatim.  Optional `#`
// padding lets a raw string contain its own delimiter:
//
//     r#"contains "quotes" inside"#
//
// The number of `#` characters between `r` and the opening quote sets the
// required padding for the closing quote; this matches Rust's familiar
// `r#"..."#` syntax.
frontends::Token PloyLexer::LexRawString() {
  core::SourceLoc loc = CurrentLoc();
  Get(); // consume the leading 'r'
  size_t hash_count = 0;
  while (!Eof() && Peek() == '#') {
    Get();
    ++hash_count;
  }
  if (Eof() || Peek() != '"') {
    // Not actually a raw string — surface as an empty kString token.  The
    // caller's identifier path normally consumes leading `r`, so reaching
    // here implies a lexer dispatch bug; emit an empty literal so the
    // parser keeps making progress.
    return frontends::Token{frontends::TokenKind::kString, "\"\"", loc};
  }
  Get(); // consume opening '"'
  std::string body;
  while (!Eof()) {
    if (Peek() == '"') {
      // Try to match the closing quote followed by `hash_count` '#'s.
      bool ok = true;
      for (size_t i = 0; i < hash_count; ++i) {
        size_t off = position_ + 1 + i;
        char la = (off < source_.size()) ? source_[off] : '\0';
        if (la != '#') { ok = false; break; }
      }
      if (ok) {
        Get(); // consume closing '"'
        for (size_t i = 0; i < hash_count; ++i) Get(); // consume '#'s
        std::string canonical;
        canonical.reserve(body.size() + 2);
        canonical.push_back('"');
        for (char ch : body) {
          switch (ch) {
          case '\n': canonical += "\\n"; break;
          case '\r': canonical += "\\r"; break;
          case '\t': canonical += "\\t"; break;
          case '\\': canonical += "\\\\"; break;
          case '"':  canonical += "\\\""; break;
          default:   canonical.push_back(ch); break;
          }
        }
        canonical.push_back('"');
        return frontends::Token{frontends::TokenKind::kString, canonical, loc};
      }
    }
    body.push_back(Get());
  }
  // Unterminated.
  std::string canonical;
  canonical.push_back('"');
  for (char ch : body) canonical.push_back(ch);
  canonical.push_back('"');
  return frontends::Token{frontends::TokenKind::kString, canonical, loc};
}

// Stand-alone entry point for triple-quoted multiline literals.  The
// dispatcher in NextToken() routes here when the first three characters are
// `"""`; the body handling is duplicated inside LexString() above for the
// case where LexString() itself notices the triple opener and recurses
// inline.
frontends::Token PloyLexer::LexMultilineString() {
  // Delegating to LexString() keeps a single body-handling implementation:
  // LexString() already detects the `"""` opener and consumes the full
  // triple-quoted span.
  return LexString();
}

// Template (interpolated) string literal (since v1.17.0): f"x = {x}".
//
// At the lexer layer we only carve out the source span and emit a single
// kString token whose lexeme starts with `f"` (or `f"""` for the multiline
// variant); the parser is responsible for splitting the body into
// alternating literal / expression segments and constructing the
// `TemplateString` AST node.  Escape handling matches the regular string
// path so `\{` / `\}` may be used to embed literal braces.
frontends::Token PloyLexer::LexTemplateString() {
  core::SourceLoc loc = CurrentLoc();
  Get(); // consume leading 'f'
  std::string lexeme = "f";
  // Optional triple-quote multiline form.
  if (Peek() == '"' && PeekNext() == '"') {
    char q1 = Get();
    char q2 = Get();
    if (Peek() == '"') {
      Get();
      lexeme += "\"\"\"";
      std::string body;
      while (!Eof()) {
        if (Peek() == '"' && PeekNext() == '"') {
          char a = Get();
          char b = Get();
          if (Peek() == '"') {
            Get();
            for (char ch : body) {
              switch (ch) {
              case '\n': lexeme += "\\n"; break;
              case '\r': lexeme += "\\r"; break;
              case '\t': lexeme += "\\t"; break;
              default:   lexeme.push_back(ch); break;
              }
            }
            lexeme += "\"\"\"";
            // Repack as canonical f"..." (single-quote canonical form) for
            // the parser; lexeme already encodes leading f""" + body + """.
            return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
          }
          body.push_back(a);
          body.push_back(b);
          continue;
        }
        body.push_back(Get());
      }
      return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
    }
    // Empty f"" — re-route through the single-quote path.
    lexeme.push_back(q1);
    lexeme.push_back(q2);
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
  }
  // Single-quote f"..." form — preserve escapes verbatim like LexString().
  if (Peek() != '"') {
    return frontends::Token{frontends::TokenKind::kString, lexeme + "\"\"", loc};
  }
  lexeme.push_back(Get()); // opening '"'
  while (!Eof() && Peek() != '"') {
    if (Peek() == '\\') {
      lexeme.push_back(Get());
      if (!Eof()) lexeme.push_back(Get());
    } else {
      lexeme.push_back(Get());
    }
  }
  if (!Eof()) lexeme.push_back(Get()); // closing '"'
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
      // Triple-slash documentation comment (since v1.18.0).  Captured
      // line-by-line into `pending_doc_` for the parser to attach to the
      // next top-level declaration.  A `////` (four slashes) sequence
      // is treated as a regular line comment per Rust convention.
      bool is_doc = (position_ + 2 < source_.size()) && source_[position_ + 2] == '/' &&
                    !((position_ + 3 < source_.size()) && source_[position_ + 3] == '/');
      Get();
      Get(); // consume '//'
      if (is_doc) {
        Get(); // consume the third '/'
        SkipDocLineComment();
      } else {
        SkipLineComment();
      }
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

  // Extended string literal prefixes (since v1.17.0).
  //
  // We must inspect the *next* character before claiming the leading 'r' /
  // 'f' as a string-literal prefix; otherwise ordinary identifiers like
  // `result` or `foo` would be mis-lexed as truncated strings.  The
  // disambiguation rule is conservative: only `r"`, `r#`, and `f"` start
  // a string-literal form — everything else falls through to the regular
  // identifier path.
  if (c == 'r' && (PeekNext() == '"' || PeekNext() == '#')) {
    return LexRawString();
  }
  if (c == 'f' && PeekNext() == '"') {
    return LexTemplateString();
  }

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
