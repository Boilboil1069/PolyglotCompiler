#include "frontends/python/include/python_lexer.h"

#include <cctype>

namespace polyglot::python {

void PythonLexer::SkipWhitespace() {
  while (std::isspace(static_cast<unsigned char>(Peek()))) {
    Get();
  }
}

void PythonLexer::SkipComment() {
  while (Peek() != '\0' && Peek() != '\n') {
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
      (lexeme == "def" || lexeme == "import" || lexeme == "from" ||
       lexeme == "return")
          ? frontends::TokenKind::kKeyword
          : frontends::TokenKind::kIdentifier;
  return frontends::Token{kind, lexeme, loc};
}

frontends::Token PythonLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  bool seen_dot = false;
  while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '.' ||
         Peek() == '_') {
    if (Peek() == '.') {
      if (seen_dot) {
        break;
      }
      seen_dot = true;
    }
    lexeme.push_back(Get());
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
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token PythonLexer::LexString() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  bool raw = false;
  bool formatted = false;
  for (;;) {
    if (Peek() == 'r' || Peek() == 'R') {
      raw = true;
      lexeme.push_back(Get());
      continue;
    }
    if (Peek() == 'f' || Peek() == 'F') {
      formatted = true;
      lexeme.push_back(Get());
      continue;
    }
    if (Peek() == 'b' || Peek() == 'B') {
      lexeme.push_back(Get());
      continue;
    }
    break;
  }
  char quote = Get();
  lexeme.push_back(quote);
  bool triple = false;
  if (Peek() == quote && PeekNext() == quote) {
    lexeme.push_back(Get());
    lexeme.push_back(Get());
    triple = true;
  }
  while (Peek() != '\0') {
    char c = Get();
    lexeme.push_back(c);
    if (!raw && c == '\\\\') {
      if (Peek() != '\0') {
        lexeme.push_back(Get());
      }
      continue;
    }
    if (formatted && c == '{' && Peek() == '{') {
      lexeme.push_back(Get());
      continue;
    }
    if (triple) {
      if (c == quote && Peek() == quote && PeekNext() == quote) {
        lexeme.push_back(Get());
        lexeme.push_back(Get());
        break;
      }
    } else if (c == quote) {
      break;
    }
  }
  return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
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
  if (c == '#') {
    Get();
    SkipComment();
    return NextToken();
  }
  if (c == 'r' || c == 'R' || c == 'f' || c == 'F' || c == 'b' || c == 'B') {
    char next = PeekNext();
    if (next == '"' || next == '\'' || next == 'r' || next == 'R' || next == 'f' ||
        next == 'F' || next == 'b' || next == 'B') {
      return LexString();
    }
  }
  if (c == '"' || c == '\'') {
    return LexString();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return LexNumber();
  }
  Get();
  return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

}  // namespace polyglot::python
