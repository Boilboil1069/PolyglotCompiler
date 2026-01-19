#include "frontends/rust/include/rust_lexer.h"

#include <cctype>

namespace polyglot::rust {

void RustLexer::SkipWhitespace() {
  while (std::isspace(static_cast<unsigned char>(Peek()))) {
    Get();
  }
}

frontends::Token RustLexer::LexIdentifierOrKeyword() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  frontends::TokenKind kind =
      (lexeme == "fn" || lexeme == "let") ? frontends::TokenKind::kKeyword
                                               : frontends::TokenKind::kIdentifier;
  return frontends::Token{kind, lexeme, loc};
}

frontends::Token RustLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isdigit(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token RustLexer::NextToken() {
  SkipWhitespace();
  core::SourceLoc loc = CurrentLoc();
  char c = Peek();
  if (c == '\0') {
    return frontends::Token{frontends::TokenKind::kEndOfFile, "", loc};
  }
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
    return LexIdentifierOrKeyword();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return LexNumber();
  }
  Get();
  return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

}  // namespace polyglot::rust
