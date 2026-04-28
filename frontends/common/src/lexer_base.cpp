/**
 * @file     lexer_base.cpp
 * @brief    Out-of-line helpers for LexerBase (token-pool mirroring).
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "frontends/common/include/lexer_base.h"
#include "frontends/common/include/token_pool.h"

namespace polyglot::frontends {

Token LexerBase::EmitToken(Token t) {
  if (token_pool_ != nullptr) {
    // Mirror into the shared pool.  We deliberately copy; the caller still
    // owns the returned Token (and most lexers go on to use it directly).
    token_pool_->Add(t);
  }
  return t;
}

} // namespace polyglot::frontends
