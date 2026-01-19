#pragma once

#include <vector>

#include "frontends/common/include/lexer_base.h"

namespace polyglot::frontends {

class TokenPool {
 public:
  const Token &Add(Token token) {
    tokens_.push_back(std::move(token));
    return tokens_.back();
  }

  const std::vector<Token> &All() const { return tokens_; }

  void Clear() { tokens_.clear(); }

 private:
  std::vector<Token> tokens_{};
};

}  // namespace polyglot::frontends
