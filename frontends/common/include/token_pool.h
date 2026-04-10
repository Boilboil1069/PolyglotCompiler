/**
 * @file     token_pool.h
 * @brief    Shared frontend infrastructure
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <vector>

#include "frontends/common/include/lexer_base.h"

namespace polyglot::frontends {

/** @brief TokenPool class. */
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
