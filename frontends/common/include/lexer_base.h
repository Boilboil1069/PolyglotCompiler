#pragma once

#include <string>

#include "common/include/core/source_loc.h"

namespace polyglot::frontends {

enum class TokenKind {
  kEndOfFile,
  kIdentifier,
  kNumber,
  kString,
  kChar,
  kLifetime,
  kKeyword,
  kSymbol,
  kPreprocessor,
  kUnknown
};

struct Token {
  TokenKind kind{TokenKind::kUnknown};
  std::string lexeme;
  core::SourceLoc loc{};
};

class LexerBase {
 public:
  virtual ~LexerBase() = default;

  virtual Token NextToken() = 0;

 protected:
  LexerBase(std::string source, std::string file)
      : source_(std::move(source)), file_(std::move(file)) {}

  char Peek() const { return position_ < source_.size() ? source_[position_] : '\0'; }
  char PeekNext() const {
    return (position_ + 1) < source_.size() ? source_[position_ + 1] : '\0';
  }

  char Get() {
    char c = Peek();
    if (c == '\n') {
      line_++;
      column_ = 1;
    } else {
      column_++;
    }
    if (position_ < source_.size()) {
      position_++;
    }
    return c;
  }

  core::SourceLoc CurrentLoc() const {
    return core::SourceLoc{file_, line_, column_};
  }

  std::string source_;
  std::string file_;
  size_t position_{0};
  size_t line_{1};
  size_t column_{1};
};

}  // namespace polyglot::frontends
