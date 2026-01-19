#include "frontends/python/include/python_lexer.h"

#include <cctype>

namespace polyglot::python {

void PythonLexer::SkipWhitespace() {
  while (std::isspace(static_cast<unsigned char>(Peek()))) {
    Get();
  }
}

frontends::Token PythonLexer::LexIdentifier() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  frontends::TokenKind kind =
      (lexeme == "def" || lexeme == "import") ? frontends::TokenKind::kKeyword
                                                  : frontends::TokenKind::kIdentifier;
  return frontends::Token{kind, lexeme, loc};
}

frontends::Token PythonLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isdigit(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token PythonLexer::NextToken() {
  SkipWhitespace();
  core::SourceLoc loc = CurrentLoc();
  char c = Peek();
  if (c == '\0') {
    return frontends::Token{frontends::TokenKind::kEndOfFile, "", loc};
  }
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
    return LexIdentifier();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return LexNumber();
  }
  Get();
  return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

}  // namespace polyglot::python
