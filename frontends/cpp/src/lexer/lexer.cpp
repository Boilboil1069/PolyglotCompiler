#include "frontends/cpp/include/cpp_lexer.h"

#include <cctype>
#include <unordered_set>
#include <vector>

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

void CppLexer::SkipBlockComment() {
  while (Peek() != '\0') {
    if (Peek() == '*' && PeekNext() == '/') {
      Get();
      Get();
      break;
    }
    Get();
  }
}

frontends::Token CppLexer::LexIdentifierOrKeyword() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  static const std::unordered_set<std::string> keywords = {
      "int", "return", "void", "struct", "class", "if", "else", "for", "while",
      "import", "using", "namespace"};
  frontends::TokenKind kind =
      keywords.count(lexeme) ? frontends::TokenKind::kKeyword
                             : frontends::TokenKind::kIdentifier;
  return frontends::Token{kind, lexeme, loc};
}

frontends::Token CppLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  if (Peek() == '0' && (PeekNext() == 'x' || PeekNext() == 'X' || PeekNext() == 'b' ||
                        PeekNext() == 'B')) {
    lexeme.push_back(Get());
    lexeme.push_back(Get());
    while (std::isxdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
      lexeme.push_back(Get());
    }
  } else {
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
  }
  while (std::isalpha(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token CppLexer::LexString() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  if (Peek() == 'R' && PeekNext() == '"') {
    lexeme.push_back(Get());
    lexeme.push_back(Get());
    std::string delimiter;
    while (Peek() != '\0' && Peek() != '(') {
      delimiter.push_back(Get());
      lexeme.push_back(delimiter.back());
    }
    if (Peek() == '(') {
      lexeme.push_back(Get());
    }
    std::string end_marker = ")" + delimiter + "\"";
    std::string window;
    while (Peek() != '\0') {
      char c = Get();
      lexeme.push_back(c);
      window.push_back(c);
      if (window.size() > end_marker.size()) {
        window.erase(window.begin());
      }
      if (window == end_marker) {
        break;
      }
    }
    return frontends::Token{frontends::TokenKind::kString, lexeme, loc};
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

frontends::Token CppLexer::LexChar() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
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

frontends::Token CppLexer::LexPreprocessor() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  while (Peek() != '\0' && Peek() != '\n') {
    lexeme.push_back(Get());
  }
  return frontends::Token{frontends::TokenKind::kPreprocessor, lexeme, loc};
}

frontends::Token CppLexer::LexOperator() {
  static const std::vector<std::string> operators = {
      ">>=", "<<=", "->*", "==", "!=", "<=", ">=", "&&", "||", "++", "--",
      "->", "::", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=",
      "^=", "##", "...", ".", "?", ":", "+", "-", "*", "/", "%", "&", "|",
      "^", "!", "~", "<", ">", "=", ",", ";", "{", "}", "(", ")", "[", "]"};
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

frontends::Token CppLexer::NextToken() {
  SkipWhitespace();
  core::SourceLoc loc = CurrentLoc();
  char c = Peek();
  if (c == '\0') {
    return frontends::Token{frontends::TokenKind::kEndOfFile, "", loc};
  }
  if (c == '#' && column_ == 1) {
    return LexPreprocessor();
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
  if (c == '"' || (c == 'R' && PeekNext() == '"')) {
    return LexString();
  }
  if (c == '\'') {
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

}  // namespace polyglot::cpp
