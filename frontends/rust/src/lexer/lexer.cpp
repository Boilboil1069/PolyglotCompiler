#include "frontends/rust/include/rust_lexer.h"

#include <cctype>
#include <unordered_set>
#include <vector>

namespace polyglot::rust {

void RustLexer::SkipWhitespace() {
  while (std::isspace(static_cast<unsigned char>(Peek()))) {
    Get();
  }
}

void RustLexer::SkipLineComment() {
  while (Peek() != '\0' && Peek() != '\n') {
    Get();
  }
}

void RustLexer::SkipBlockComment() {
  int depth = 1;
  while (Peek() != '\0' && depth > 0) {
    if (Peek() == '/' && PeekNext() == '*') {
      Get();
      Get();
      depth++;
      continue;
    }
    if (Peek() == '*' && PeekNext() == '/') {
      Get();
      Get();
      depth--;
      continue;
    }
    Get();
  }
}

static bool IsLineDocStart(const std::string &src, size_t pos) {
  return pos + 2 < src.size() && src[pos] == '/' && src[pos + 1] == '/' &&
         (src[pos + 2] == '/' || src[pos + 2] == '!');
}

static bool IsBlockDocStart(const std::string &src, size_t pos) {
  return pos + 2 < src.size() && src[pos] == '/' && src[pos + 1] == '*' &&
         (src[pos + 2] == '*' || src[pos + 2] == '!');
}

frontends::Token LexDocLine(const std::string &source, const std::string &file,
                            size_t &position, size_t &line, size_t &column) {
  size_t start_pos = position;
  size_t start_col = column;
  size_t start_line = line;
  // consume '//' and third char
  position += 3;
  column += 3;
  while (position < source.size() && source[position] != '\n') {
    position++;
    column++;
  }
  std::string text = source.substr(start_pos, position - start_pos);
  return frontends::Token{frontends::TokenKind::kComment,
                          text, core::SourceLoc{file, start_line, start_col}, true};
}

frontends::Token LexDocBlock(const std::string &source, const std::string &file,
                             size_t &position, size_t &line, size_t &column) {
  size_t start_pos = position;
  size_t start_col = column;
  size_t start_line = line;
  int depth = 0;
  while (position < source.size()) {
    if (source[position] == '/' && position + 1 < source.size() && source[position + 1] == '*') {
      depth++;
      position += 2;
      column += 2;
      continue;
    }
    if (source[position] == '*' && position + 1 < source.size() && source[position + 1] == '/') {
      depth--;
      position += 2;
      column += 2;
      if (depth <= 0) break;
      continue;
    }
    if (source[position] == '\n') {
      line++;
      column = 1;
      position++;
      continue;
    }
    position++;
    column++;
  }
  std::string text = source.substr(start_pos, position - start_pos);
  return frontends::Token{frontends::TokenKind::kComment,
                          text, core::SourceLoc{file, start_line, start_col}, true};
}

frontends::Token RustLexer::LexIdentifierOrKeyword() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  static const std::unordered_set<std::string> keywords = {
      "as",   "async", "await", "break", "const",   "continue", "crate", "dyn",
      "else", "enum",  "extern", "false",  "fn",     "for",      "if",   "impl",
      "in",   "let",   "loop",   "match",  "mod",    "move",     "mut",  "pub",
      "ref",  "return","self",   "Self",   "static", "struct",   "super","trait",
      "true", "type",  "unsafe", "use",    "where",  "while",    "yield"};
  frontends::TokenKind kind =
      keywords.count(lexeme) ? frontends::TokenKind::kKeyword
                             : frontends::TokenKind::kIdentifier;
  return frontends::Token{kind, lexeme, loc};
}

frontends::Token RustLexer::LexRawIdentifier() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  Get();
  Get();
  while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kIdentifier, lexeme, loc};
}

frontends::Token RustLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  auto consume_digits = [&](auto predicate) {
    while (predicate(static_cast<unsigned char>(Peek())) || Peek() == '_') {
      lexeme.push_back(Get());
    }
  };

  if (Peek() == '0' && (PeekNext() == 'x' || PeekNext() == 'X')) {
    lexeme.push_back(Get());
    lexeme.push_back(Get());
    consume_digits([](unsigned char c) { return std::isxdigit(c); });
  } else if (Peek() == '0' && (PeekNext() == 'b' || PeekNext() == 'B')) {
    lexeme.push_back(Get());
    lexeme.push_back(Get());
    consume_digits([](unsigned char c) { return c == '0' || c == '1'; });
  } else if (Peek() == '0' && (PeekNext() == 'o' || PeekNext() == 'O')) {
    lexeme.push_back(Get());
    lexeme.push_back(Get());
    consume_digits([](unsigned char c) { return c >= '0' && c <= '7'; });
  } else {
    consume_digits([](unsigned char c) { return std::isdigit(c); });
    if (Peek() == '.') {
      lexeme.push_back(Get());
      consume_digits([](unsigned char c) { return std::isdigit(c); });
    }
    if (Peek() == 'e' || Peek() == 'E') {
      lexeme.push_back(Get());
      if (Peek() == '+' || Peek() == '-') {
        lexeme.push_back(Get());
      }
      consume_digits([](unsigned char c) { return std::isdigit(c); });
    }
  }

  // type suffix
  while (std::isalpha(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token RustLexer::LexString() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  bool byte_prefix = false;
  if (Peek() == 'b' && (PeekNext() == '"' || PeekNext() == 'r')) {
    byte_prefix = true;
    lexeme.push_back(Get());
  }
  if (Peek() == 'r') {
    lexeme.push_back(Get());
    size_t hash_count = 0;
    while (Peek() == '#') {
      lexeme.push_back(Get());
      hash_count++;
    }
    if (Peek() == '"') {
      lexeme.push_back(Get());
      while (Peek() != '\0') {
        char c = Get();
        lexeme.push_back(c);
        if (c == '"' && hash_count > 0) {
          size_t matched = 0;
          while (Peek() == '#' && matched < hash_count) {
            lexeme.push_back(Get());
            matched++;
          }
          if (matched == hash_count) {
            break;
          }
        } else if (c == '"' && hash_count == 0) {
          break;
        }
      }
      return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
    }
  }
  char quote = Get();
  lexeme.push_back(quote);
  while (Peek() != '\0') {
    char c = Get();
    lexeme.push_back(c);
    if (c == '\\') {
      if (Peek() != '\0') {
        lexeme.push_back(Get());
      }
      continue;
    }
    if (c == quote) {
      break;
    }
  }
  return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
}

frontends::Token RustLexer::LexChar() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  if (Peek() == 'b' && PeekNext() == '\'') {
    lexeme.push_back(Get());
  }
  char quote = Get();
  lexeme.push_back(quote);
  while (Peek() != '\0') {
    char c = Get();
    lexeme.push_back(c);
    if (c == '\\\\') {
      if (Peek() != '\0') {
        lexeme.push_back(Get());
      }
      continue;
    }
    if (c == quote) {
      break;
    }
  }
  return frontends::Token{frontends::TokenKind::kChar, lexeme, loc};
}

frontends::Token RustLexer::LexOperator() {
  static const std::vector<std::string> operators = {
  "=>", "::", "->", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=",
  "-=", "*=", "/=", "%=", "&=", "|=", "^=", "..=", "..", "...", ".", "?", ":", "+",
  "-", "*", "/", "%", "&", "|", "^", "!", "~", "<", ">", "=", "@", ",", ";", "{",
  "}", "(", ")", "[", "]"};
  for (const auto &op : operators) {
    if (source_.compare(position_, op.size(), op) == 0) {
      core::SourceLoc loc = CurrentLoc();
      for (size_t i = 0; i < op.size(); ++i) {
        Get();
      }
      return frontends::Token{frontends::TokenKind::kSymbol, op, loc};
    }
  }
  core::SourceLoc loc = CurrentLoc();
  char c = Get();
  return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

frontends::Token RustLexer::NextToken() {
  if (IsLineDocStart(source_, position_)) {
    return LexDocLine(source_, file_, position_, line_, column_);
  }
  if (IsBlockDocStart(source_, position_)) {
    return LexDocBlock(source_, file_, position_, line_, column_);
  }
  SkipWhitespace();
  core::SourceLoc loc = CurrentLoc();
  char c = Peek();
  if (c == '\0') {
    return frontends::Token{frontends::TokenKind::kEndOfFile, "", loc};
  }
  if (c == '/' && PeekNext() == '/') {
    Get();
    Get();
    SkipLineComment();
    return NextToken();
  }
  if (c == '/' && PeekNext() == '*') {
    Get();
    Get();
    SkipBlockComment();
    return NextToken();
  }
  if (c == 'r' && PeekNext() == '#') {
    return LexRawIdentifier();
  }
  if (c == '"' || (c == 'b' && PeekNext() == '"') ||
      (c == 'r' && (PeekNext() == '"' || PeekNext() == '#'))) {
    return LexString();
  }
  if (c == 'b' && PeekNext() == '\'') {
    return LexChar();
  }
  if (c == '\'') {
    if (std::isalpha(static_cast<unsigned char>(PeekNext())) &&
        PeekNext() != '\0' && source_.size() > position_ + 2 &&
        source_[position_ + 2] != '\'') {
      core::SourceLoc life_loc = CurrentLoc();
      Get();
      std::string name;
      while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
        name.push_back(Get());
      }
      return frontends::Token{frontends::TokenKind::kLifetime, name, life_loc};
    }
    return LexChar();
  }
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
    return LexIdentifierOrKeyword();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return LexNumber();
  }
  (void)loc;
  return LexOperator();
}

}  // namespace polyglot::rust
