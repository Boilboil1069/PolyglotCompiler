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
  while (Peek() != '\0') {
    if (Peek() == '*' && PeekNext() == '/') {
      Get();
      Get();
      break;
    }
    Get();
  }
}

frontends::Token RustLexer::LexIdentifierOrKeyword() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  static const std::unordered_set<std::string> keywords = {
      "fn", "let", "mut", "struct", "enum", "impl", "match", "if", "else", "use",
      "mod", "return"};
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
  while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  if (Peek() == '.') {
    lexeme.push_back(Get());
    while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
      lexeme.push_back(Get());
    }
  }
  if (Peek() == 'e' || Peek() == 'E') {
    lexeme.push_back(Get());
    if (Peek() == '+' || Peek() == '-') {
      lexeme.push_back(Get());
    }
    while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
      lexeme.push_back(Get());
    }
  }
  while (std::isalpha(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token RustLexer::LexString() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  if (Peek() == 'b' && PeekNext() == '"') {
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
      "-=", "*=", "/=", "%=", "&=", "|=", "^=", "..", "...", ".", "?", ":", "+",
      "-", "*", "/", "%", "&", "|", "^", "!", "~", "<", ">", "=", ",", ";", "{",
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
