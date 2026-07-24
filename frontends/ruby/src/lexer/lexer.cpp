/**
 * @file     lexer.cpp
 * @brief    Ruby tokenizer (2.7+ / 3.x)
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

#include "frontends/ruby/include/ruby_lexer.h"

namespace polyglot::ruby {

namespace {

const std::unordered_set<std::string> &Keywords() {
  static const std::unordered_set<std::string> kw = {
      "BEGIN",  "END",   "alias",    "and",      "begin",    "break",       "case",
      "class",  "def",   "defined?", "do",       "else",     "elsif",       "end",
      "ensure", "false", "for",      "if",       "in",       "module",      "next",
      "nil",    "not",   "or",       "redo",     "rescue",   "retry",       "return",
      "self",   "super", "then",     "true",     "undef",    "unless",      "until",
      "when",   "while", "yield",    "__FILE__", "__LINE__", "__ENCODING__"};
  return kw;
}

bool IsIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool IsIdentPart(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

bool RbLexer::AtLineStart() const {
  if (position_ == 0)
    return true;
  size_t p = position_;
  while (p > 0) {
    char c = source_[p - 1];
    if (c == '\n')
      return true;
    if (c != ' ' && c != '\t')
      return false;
    --p;
  }
  return true;
}

bool RbLexer::LooksLikeHeredoc() const {
  if (!prev_allows_unary_ || Peek() != '<' || PeekNext() != '<')
    return false;
  size_t p = position_ + 2;
  if (p < source_.size() && (source_[p] == '-' || source_[p] == '~'))
    ++p;
  if (p >= source_.size())
    return false;

  char quote = '\0';
  if (source_[p] == '\'' || source_[p] == '"' || source_[p] == '`')
    quote = source_[p++];
  if (p >= source_.size() || !IsIdentStart(source_[p]))
    return false;
  while (p < source_.size() && IsIdentPart(source_[p]))
    ++p;
  if (quote != '\0') {
    if (p >= source_.size() || source_[p] != quote)
      return false;
    ++p;
  }
  while (p < source_.size() && (source_[p] == ' ' || source_[p] == '\t' || source_[p] == '\r'))
    ++p;
  return p >= source_.size() || source_[p] == '\n';
}

frontends::Token RbLexer::LexHeredoc() {
  auto loc = CurrentLoc();
  Get();
  Get(); // <<
  bool allow_indented_terminator = false;
  bool strip_indent = false;
  if (Peek() == '-' || Peek() == '~') {
    allow_indented_terminator = true;
    strip_indent = Peek() == '~';
    Get();
  }

  char quote = '\0';
  if (Peek() == '\'' || Peek() == '"' || Peek() == '`')
    quote = Get();
  bool allow_interpolation = quote != '\'';
  std::string tag;
  while (!Eof() && IsIdentPart(Peek()))
    tag.push_back(Get());
  if (quote != '\0' && Peek() == quote)
    Get();
  while (!Eof() && Peek() != '\n')
    Get();
  if (Peek() == '\n')
    Get();

  std::vector<std::string> lines;
  bool terminated = false;
  while (!Eof()) {
    std::string line;
    while (!Eof() && Peek() != '\n' && Peek() != '\r')
      line.push_back(Get());
    if (Peek() == '\r')
      Get();
    bool had_newline = Peek() == '\n';
    if (had_newline)
      Get();

    std::string terminator = line;
    if (allow_indented_terminator) {
      size_t first = terminator.find_first_not_of(" \t");
      terminator = first == std::string::npos ? "" : terminator.substr(first);
    }
    if (terminator == tag) {
      terminated = true;
      pending_newline_ = had_newline;
      break;
    }
    lines.push_back(line + (had_newline ? "\n" : ""));
  }

  if (strip_indent) {
    size_t common = std::string::npos;
    for (const auto &line : lines) {
      size_t count = 0;
      while (count < line.size() && (line[count] == ' ' || line[count] == '\t'))
        ++count;
      size_t content = line.find_first_not_of(" \t\r\n");
      if (content != std::string::npos)
        common = common == std::string::npos ? count : std::min(common, count);
    }
    if (common != std::string::npos && common > 0) {
      for (auto &line : lines)
        line.erase(0, std::min(common, line.find_first_not_of(" \t")));
    }
  }

  std::string body;
  for (const auto &line : lines)
    body += line;
  frontends::Token token{frontends::TokenKind::kString, body, loc};
  if (!terminated)
    token.raw_lexeme = "__polyglot_ruby_unterminated_heredoc__";
  else if (allow_interpolation && body.find("#{") != std::string::npos)
    token.raw_lexeme = "__polyglot_ruby_interpolated_heredoc__";
  else
    token.raw_lexeme = "__polyglot_ruby_heredoc__";
  prev_allows_unary_ = false;
  return token;
}

void RbLexer::SkipSpacesAndContinuations() {
  while (!Eof()) {
    char c = Peek();
    if (c == ' ' || c == '\t' || c == '\r') {
      Get();
      continue;
    }
    if (c == '\\' && PeekNext() == '\n') {
      Get();
      Get();
      continue;
    }
    break;
  }
}

void RbLexer::SkipLineComment(bool *is_yard) {
  // Already on '#'
  Get();
  std::string body;
  while (!Eof() && Peek() != '\n')
    body.push_back(Get());
  // YARD tags begin with @param / @return / @yield etc.
  auto pos = body.find_first_not_of(" \t");
  if (pos != std::string::npos && body[pos] == '@') {
    if (is_yard)
      *is_yard = true;
    if (!pending_doc_.empty())
      pending_doc_.push_back('\n');
    pending_doc_ += body;
  } else if (is_yard) {
    *is_yard = false;
  }
}

frontends::Token RbLexer::LexIdentifierOrKeyword() {
  auto loc = CurrentLoc();
  std::string s;
  if (Peek() == '@') {
    s.push_back(Get());
    if (Peek() == '@')
      s.push_back(Get());
  }
  if (Peek() == '$')
    s.push_back(Get());
  while (!Eof() && IsIdentPart(Peek()))
    s.push_back(Get());
  // method names may end with ? or !
  if (!Eof() && (Peek() == '?' || Peek() == '!'))
    s.push_back(Get());
  frontends::Token t;
  t.loc = loc;
  t.lexeme = s;
  if (s == "defined?") {
    t.kind = frontends::TokenKind::kKeyword;
  } else if (Keywords().count(s) > 0) {
    t.kind = frontends::TokenKind::kKeyword;
  } else {
    t.kind = frontends::TokenKind::kIdentifier;
  }
  prev_allows_unary_ = (t.kind == frontends::TokenKind::kKeyword);
  return t;
}

frontends::Token RbLexer::LexNumber() {
  auto loc = CurrentLoc();
  std::string s;
  bool is_float = false;
  if (Peek() == '0' && (PeekNext() == 'x' || PeekNext() == 'X' || PeekNext() == 'b' ||
                        PeekNext() == 'B' || PeekNext() == 'o' || PeekNext() == 'O')) {
    s.push_back(Get());
    s.push_back(Get());
    while (!Eof() && (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
      if (Peek() != '_')
        s.push_back(Peek());
      Get();
    }
  } else {
    while (!Eof() && (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
      if (Peek() != '_')
        s.push_back(Peek());
      Get();
    }
    if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(PeekNext()))) {
      is_float = true;
      s.push_back(Get());
      while (!Eof() && (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
        if (Peek() != '_')
          s.push_back(Peek());
        Get();
      }
    }
    if (Peek() == 'e' || Peek() == 'E') {
      is_float = true;
      s.push_back(Get());
      if (Peek() == '+' || Peek() == '-')
        s.push_back(Get());
      while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek())))
        s.push_back(Get());
    }
  }
  (void)is_float;
  frontends::Token t;
  t.kind = frontends::TokenKind::kNumber;
  t.loc = loc;
  t.lexeme = s;
  prev_allows_unary_ = false;
  return t;
}

frontends::Token RbLexer::LexString(char quote) {
  auto loc = CurrentLoc();
  std::string s;
  s.push_back(Get()); // opening quote
  while (!Eof() && Peek() != quote) {
    if (Peek() == '\\') {
      s.push_back(Get());
      if (!Eof())
        s.push_back(Get());
      continue;
    }
    if (quote == '"' && Peek() == '#' && PeekNext() == '{') {
      // Capture interpolation block opaquely
      s.push_back(Get());
      s.push_back(Get());
      int depth = 1;
      while (!Eof() && depth > 0) {
        if (Peek() == '{')
          depth++;
        else if (Peek() == '}')
          depth--;
        if (depth > 0)
          s.push_back(Get());
        else {
          s.push_back(Get());
          break;
        }
      }
      continue;
    }
    s.push_back(Get());
  }
  if (!Eof())
    s.push_back(Get());
  frontends::Token t;
  t.kind = frontends::TokenKind::kString;
  t.loc = loc;
  t.lexeme = s;
  prev_allows_unary_ = false;
  return t;
}

frontends::Token RbLexer::LexSymbol() {
  auto loc = CurrentLoc();
  std::string s;
  s.push_back(Get()); // ':'
  if (Peek() == '"') {
    // :"quoted symbol"
    s.push_back(Get());
    while (!Eof() && Peek() != '"') {
      if (Peek() == '\\') {
        s.push_back(Get());
        if (!Eof())
          s.push_back(Get());
        continue;
      }
      s.push_back(Get());
    }
    if (!Eof())
      s.push_back(Get());
  } else {
    while (!Eof() && (IsIdentPart(Peek()) || Peek() == '?' || Peek() == '!' || Peek() == '=')) {
      s.push_back(Get());
    }
  }
  frontends::Token t;
  t.kind = frontends::TokenKind::kString;
  t.loc = loc;
  t.lexeme = s;
  prev_allows_unary_ = false;
  return t;
}

frontends::Token RbLexer::LexOperator() {
  auto loc = CurrentLoc();
  static const std::vector<std::string> three = {
      "**=", "<<=", ">>=", "===", "<=>", "&&=", "||=", "..."};
  static const std::vector<std::string> two = {"**", "==", "!=", "<=", ">=", "<<", ">>", "&&",
                                               "||", "+=", "-=", "*=", "/=", "%=", "|=", "&=",
                                               "^=", "::", "..", "=>", "->", "&.", "=~", "!~"};
  if (position_ + 2 < source_.size()) {
    std::string s3 = source_.substr(position_, 3);
    for (auto &op : three)
      if (s3 == op) {
        Get();
        Get();
        Get();
        frontends::Token t;
        t.kind = frontends::TokenKind::kSymbol;
        t.lexeme = s3;
        t.loc = loc;
        prev_allows_unary_ = true;
        return t;
      }
  }
  if (position_ + 1 < source_.size()) {
    std::string s2 = source_.substr(position_, 2);
    for (auto &op : two)
      if (s2 == op) {
        Get();
        Get();
        frontends::Token t;
        t.kind = frontends::TokenKind::kSymbol;
        t.lexeme = s2;
        t.loc = loc;
        prev_allows_unary_ = true;
        return t;
      }
  }
  char c = Get();
  frontends::Token t;
  t.kind = frontends::TokenKind::kSymbol;
  t.lexeme = std::string(1, c);
  t.loc = loc;
  // After closing bracket / identifier, '-' is binary; we set prev_allows_unary_
  // accordingly in the dispatcher.
  prev_allows_unary_ = (c != ')' && c != ']' && c != '}');
  return t;
}

frontends::Token RbLexer::NextToken() {
  if (pending_newline_) {
    pending_newline_ = false;
    frontends::Token token;
    token.kind = frontends::TokenKind::kNewline;
    token.lexeme = "\n";
    token.loc = CurrentLoc();
    prev_allows_unary_ = true;
    return token;
  }
  while (true) {
    SkipSpacesAndContinuations();
    if (Eof()) {
      frontends::Token t;
      t.kind = frontends::TokenKind::kEndOfFile;
      t.loc = CurrentLoc();
      return t;
    }
    char c = Peek();
    if (c == '#') {
      bool is_yard;
      SkipLineComment(&is_yard);
      continue;
    }
    if (c == '\n') {
      auto loc = CurrentLoc();
      Get();
      frontends::Token t;
      t.kind = frontends::TokenKind::kNewline;
      t.lexeme = "\n";
      t.loc = loc;
      prev_allows_unary_ = true;
      return t;
    }
    if (c == ';') {
      auto loc = CurrentLoc();
      Get();
      frontends::Token t;
      t.kind = frontends::TokenKind::kSymbol;
      t.lexeme = ";";
      t.loc = loc;
      prev_allows_unary_ = true;
      return t;
    }
    if (IsIdentStart(c) || c == '@' || c == '$')
      return LexIdentifierOrKeyword();
    if (std::isdigit(static_cast<unsigned char>(c)))
      return LexNumber();
    if (c == '"' || c == '\'')
      return LexString(c);
    if (c == ':' && (IsIdentStart(PeekNext()) || PeekNext() == '"'))
      return LexSymbol();
    if (c == '<' && PeekNext() == '<' && LooksLikeHeredoc())
      return LexHeredoc();
    return LexOperator();
  }
}

} // namespace polyglot::ruby
