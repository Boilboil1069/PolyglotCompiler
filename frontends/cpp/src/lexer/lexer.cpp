#include "frontends/cpp/include/cpp_lexer.h"

#include <cctype>

namespace polyglot::cpp {

void CppLexer::SkipWhitespace() {
  while (std::isspace(static_cast<unsigned char>(Peek()))) {
    Get();
  }
}

void CppLexer::SkipLineComment() {
  while (Peek() != '\0' && Peek() != '\n') {
    Get();
  }
}

frontends::Token CppLexer::LexIdentifierOrKeyword() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  frontends::TokenKind kind =
      (lexeme == "int" || lexeme == "return") ? frontends::TokenKind::kKeyword
                                                  : frontends::TokenKind::kIdentifier;
  return frontends::Token{kind, lexeme, loc};
}

frontends::Token CppLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isdigit(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token CppLexer::NextToken() {
  SkipWhitespace();
  core::SourceLoc loc = CurrentLoc();
  char c = Peek();
  if (c == '\0') {
    return frontends::Token{frontends::TokenKind::kEndOfFile, "", loc};
  }
  if (c == '/' && source_.size() > position_ + 1 && source_[position_ + 1] == '/') {
    Get();
    Get();
    SkipLineComment();
    return NextToken();
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

}  // namespace polyglot::cpp
