#include "frontends/cpp/include/cpp_lexer.h"

#include <cctype>
#include <unordered_set>
#include <vector>

namespace polyglot::cpp {

static bool IsIdentStart(unsigned char c) {
  return std::isalpha(c) || c == '_' || c >= 0x80;
}

static bool IsIdentContinue(unsigned char c) {
  return std::isalnum(c) || c == '_' || c >= 0x80;
}

void CppLexer::SkipWhitespace() {
  while (true) {
    if (Peek() == '\\' && PeekNext() == '\n') {
      Get();
      Get();
      continue;
    }
    if (!std::isspace(static_cast<unsigned char>(Peek()))) {
      break;
    }
    Get();
  }
}

bool CppLexer::StartsWithStringPrefix() const {
  if (Peek() == 'u') {
    if (PeekNext() == '8') {
      char c3 = position_ + 2 < source_.size() ? source_[position_ + 2] : '\0';
      return c3 == '"' || c3 == '\'' || c3 == 'R';
    }
    char c2 = PeekNext();
    return c2 == '"' || c2 == '\'' || c2 == 'R';
  }
  if (Peek() == 'U' || Peek() == 'L') {
    char c2 = PeekNext();
    return c2 == '"' || c2 == '\'' || c2 == 'R';
  }
  if (Peek() == 'R') {
    char c2 = PeekNext();
    return c2 == '"';
  }
  return false;
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
  while (IsIdentContinue(static_cast<unsigned char>(Peek()))) {
    lexeme.push_back(Get());
  }
  static const std::unordered_set<std::string> keywords = {
      "alignas",   "alignof",      "asm",       "auto",        "bool",       "break",
      "case",      "catch",        "char",      "char8_t",     "char16_t",   "char32_t",
      "class",     "const",        "consteval", "constexpr",   "constinit",  "continue",
      "co_await",  "co_return",    "co_yield",  "concept",     "decltype",    "default",
      "delete",    "do",           "double",    "dynamic_cast", "else",       "enum",
      "explicit",  "export",       "extern",    "false",       "float",       "for",
      "friend",    "goto",         "if",        "import",      "inline",      "int",
      "long",      "module",       "mutable",   "namespace",   "new",         "noexcept",
      "nullptr",   "operator",     "private",   "protected",   "public",      "register",
      "reinterpret_cast", "requires", "return", "short",       "signed",      "sizeof",
      "static",    "static_assert", "static_cast", "struct",   "switch",      "template",
      "this",      "thread_local", "throw",     "true",        "try",         "typedef",
      "typeid",    "typename",     "union",     "unsigned",    "using",       "virtual",
      "void",      "volatile",     "wchar_t",   "while"};
  frontends::TokenKind kind =
      keywords.count(lexeme) ? frontends::TokenKind::kKeyword
                             : frontends::TokenKind::kIdentifier;
  return frontends::Token{kind, lexeme, loc};
}

frontends::Token CppLexer::LexNumber() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  bool is_float = false;

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
      is_float = true;
      lexeme.push_back(Get());
      consume_digits([](unsigned char c) { return std::isdigit(c); });
    }
    if (Peek() == 'e' || Peek() == 'E') {
      is_float = true;
      lexeme.push_back(Get());
      if (Peek() == '+' || Peek() == '-') {
        lexeme.push_back(Get());
      }
      consume_digits([](unsigned char c) { return std::isdigit(c); });
    }
  }

  while (std::isalpha(static_cast<unsigned char>(Peek())) || Peek() == '_') {
    lexeme.push_back(Get());
  }
  (void)is_float;
  return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token CppLexer::LexString() {
  core::SourceLoc loc = CurrentLoc();
  std::string lexeme;
  bool raw = false;

  if (Peek() == 'u') {
    lexeme.push_back(Get());
    if (Peek() == '8') {
      lexeme.push_back(Get());
    }
  } else if (Peek() == 'U' || Peek() == 'L') {
    lexeme.push_back(Get());
  }
  if (Peek() == 'R') {
    raw = true;
    lexeme.push_back(Get());
  }

  char quote = Get();
  lexeme.push_back(quote);

  if (raw && quote == '"') {
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
    while (!Eof()) {
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

  while (!Eof()) {
    char c = Get();
    lexeme.push_back(c);
    if (c == '\\') {
      if (!Eof()) {
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
  if (Peek() == 'u') {
    lexeme.push_back(Get());
    if (Peek() == '8') {
      lexeme.push_back(Get());
    }
  } else if (Peek() == 'U' || Peek() == 'L') {
    lexeme.push_back(Get());
  }
  char quote = Get();
  lexeme.push_back(quote);
  while (!Eof()) {
    char c = Get();
    lexeme.push_back(c);
    if (c == '\\') {
      if (!Eof()) {
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
  auto push = [&](frontends::TokenKind kind, std::string text, core::SourceLoc l) {
    pending_.push_back(frontends::Token{kind, std::move(text), l});
  };

  static const std::vector<std::string> operators = {
    "<=>", ">>=", "<<=", "->*", "##",  "...", ">>", "<<", "==", "!=", "<=", ">=",
    "&&",  "||",  "++",  "--",  "->",  "::",  ".*", "+=", "-=", "*=", "/=", "%=",
    "&=",  "|=",  "^=",  ".",   "?",   ":",   "+",  "-",  "*",  "/",  "%",  "&",
    "|",   "^",   "!",   "~",   "<",   ">",   "=",  ",",  ";",  "{",  "}",  "(",
    ")",   "[",   "]",   "#"};

  auto match_operator = [&](core::SourceLoc l) -> bool {
    for (const auto &op : operators) {
      if (source_.compare(position_, op.size(), op) == 0) {
        for (size_t i = 0; i < op.size(); ++i) {
          Get();
        }
        push(frontends::TokenKind::kSymbol, op, l);
        return true;
      }
    }
    return false;
  };

  // consume '#'
  push(frontends::TokenKind::kPreprocessor, "#", loc);
  Get();

  while (Peek() == ' ' || Peek() == '\t') {
    Get();
  }

  auto read_identifier = [&]() {
    core::SourceLoc l = CurrentLoc();
    std::string id;
    while (IsIdentContinue(static_cast<unsigned char>(Peek()))) {
      id.push_back(Get());
    }
    if (!id.empty()) {
      push(frontends::TokenKind::kPreprocessor, id, l);
    }
  };

  auto read_number = [&]() {
    core::SourceLoc l = CurrentLoc();
    std::string num;
    while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_') {
      num.push_back(Get());
    }
    if (!num.empty()) {
      push(frontends::TokenKind::kNumber, num, l);
    }
  };

  while (!Eof()) {
    if (Peek() == '\n') {
      Get();
      break;
    }
    if (Peek() == '\\' && PeekNext() == '\n') {
      Get();
      Get();
      continue;
    }
    if (Peek() == ' ' || Peek() == '\t') {
      Get();
      continue;
    }
    if (IsIdentStart(static_cast<unsigned char>(Peek()))) {
      read_identifier();
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(Peek()))) {
      read_number();
      continue;
    }
    if (Peek() == '"' || Peek() == '\'' || StartsWithStringPrefix()) {
      auto str_tok = LexString();
      push(str_tok.kind, str_tok.lexeme, str_tok.loc);
      continue;
    }

    core::SourceLoc op_loc = CurrentLoc();
    if (source_.compare(position_, 4, "%:%:") == 0) {
      Get();
      Get();
      Get();
      Get();
      push(frontends::TokenKind::kSymbol, "##", op_loc);
      continue;
    }
    if (source_.compare(position_, 2, "%:") == 0) {
      Get();
      Get();
      push(frontends::TokenKind::kSymbol, "#", op_loc);
      continue;
    }
    if (source_.compare(position_, 2, "<:") == 0) {
      Get();
      Get();
      push(frontends::TokenKind::kSymbol, "[", op_loc);
      continue;
    }
    if (source_.compare(position_, 2, ":>") == 0) {
      Get();
      Get();
      push(frontends::TokenKind::kSymbol, "]", op_loc);
      continue;
    }
    if (source_.compare(position_, 2, "<%") == 0) {
      Get();
      Get();
      push(frontends::TokenKind::kSymbol, "{", op_loc);
      continue;
    }
    if (source_.compare(position_, 2, "%>") == 0) {
      Get();
      Get();
      push(frontends::TokenKind::kSymbol, "}", op_loc);
      continue;
    }
    if (Peek() == '?' && PeekNext() == '?' && position_ + 2 < source_.size()) {
      char third = source_[position_ + 2];
      char mapped = '\0';
      switch (third) {
        case '=': mapped = '#'; break;
        case '/': mapped = '\\'; break;
        case '\'': mapped = '^'; break;
        case '(': mapped = '['; break;
        case ')': mapped = ']'; break;
        case '!': mapped = '|'; break;
        case '<': mapped = '{'; break;
        case '>': mapped = '}'; break;
        case '-': mapped = '~'; break;
        default: break;
      }
      if (mapped != '\0') {
        Get();
        Get();
        Get();
        push(frontends::TokenKind::kSymbol, std::string(1, mapped), op_loc);
        continue;
      }
    }
    if (match_operator(op_loc)) {
      continue;
    }

    char c = Get();
    push(frontends::TokenKind::kSymbol, std::string(1, c), op_loc);
  }

  auto first = pending_.front();
  pending_.erase(pending_.begin());
  return first;
}

frontends::Token CppLexer::LexOperator() {
  core::SourceLoc loc = CurrentLoc();
  if (source_.compare(position_, 4, "%:%:") == 0) {
    Get();
    Get();
    Get();
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, "##", loc};
  }
  if (source_.compare(position_, 2, "%:") == 0) {
    Get();
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, "#", loc};
  }
  if (source_.compare(position_, 2, "<:") == 0) {
    Get();
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, "[", loc};
  }
  if (source_.compare(position_, 2, ":>") == 0) {
    Get();
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, "]", loc};
  }
  if (source_.compare(position_, 2, "<%") == 0) {
    Get();
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, "{", loc};
  }
  if (source_.compare(position_, 2, "%>") == 0) {
    Get();
    Get();
    return frontends::Token{frontends::TokenKind::kSymbol, "}", loc};
  }
  if (Peek() == '?' && PeekNext() == '?' && position_ + 2 < source_.size()) {
    char third = source_[position_ + 2];
    char mapped = '\0';
    switch (third) {
      case '=': mapped = '#'; break;
      case '/': mapped = '\\'; break;
      case '\'': mapped = '^'; break;
      case '(': mapped = '['; break;
      case ')': mapped = ']'; break;
      case '!': mapped = '|'; break;
      case '<': mapped = '{'; break;
      case '>': mapped = '}'; break;
      case '-': mapped = '~'; break;
      default: break;
    }
    if (mapped != '\0') {
      Get();
      Get();
      Get();
      return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, mapped), loc};
    }
  }
  static const std::vector<std::string> operators = {
    "<=>", ">>=", "<<=", "->*", "##",  "...", ">>", "<<", "==", "!=", "<=", ">=",
    "&&",  "||",  "++",  "--",  "->",  "::",  ".*", "+=", "-=", "*=", "/=", "%=",
    "&=",  "|=",  "^=",  ".",   "?",   ":",   "+",  "-",  "*",  "/",  "%",  "&",
    "|",   "^",   "!",   "~",   "<",   ">",   "=",  ",",  ";",  "{",  "}",  "(",
    ")",   "[",   "]"};
  for (const auto &op : operators) {
    if (source_.compare(position_, op.size(), op) == 0) {
      for (size_t i = 0; i < op.size(); ++i) {
        Get();
      }
      return frontends::Token{frontends::TokenKind::kSymbol, op, loc};
    }
  }
  char c = Get();
  return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

frontends::Token CppLexer::NextToken() {
  if (!pending_.empty()) {
    auto tok = pending_.front();
    pending_.erase(pending_.begin());
    return tok;
  }

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
  if (c == '"' || (c == 'R' && PeekNext() == '"') || StartsWithStringPrefix()) {
    return LexString();
  }
  if (c == '\'' || (StartsWithStringPrefix() && (PeekNext() == '\'' ||
      (Peek() == 'u' && position_ + 2 < source_.size() && source_[position_ + 2] == '\'')))) {
    return LexChar();
  }
  if (IsIdentStart(static_cast<unsigned char>(c))) {
    return LexIdentifierOrKeyword();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return LexNumber();
  }
  (void)loc;
  return LexOperator();
}

}  // namespace polyglot::cpp
