/**
 * @file     lexer_base.h
 * @brief    Shared frontend infrastructure
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>

#include "common/include/core/source_loc.h"

namespace polyglot::frontends {

class SharedTokenPool; // forward decl — see token_pool.h


/** @brief TokenKind enumeration. */
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
  kComment,
  kNewline,
  kIndent,
  kDedent,
  kUnknown
};

/** @brief Token data structure. */
struct Token {
  TokenKind kind{TokenKind::kUnknown};
  // Canonical lexeme.  For keywords this is the language's canonical spelling
  // (e.g. always upper-case in Ploy), so that downstream parser / sema code
  // can compare against a single fixed string regardless of how the user
  // typed the keyword in source.  For identifiers, numbers, strings,
  // operators and punctuation this stores the source text verbatim.
  std::string lexeme;
  core::SourceLoc loc{};
  // Optional metadata for tokens (e.g., doc comments)
  bool is_doc{false};
  // Original source text exactly as it appeared in the input buffer.  When
  // empty (the common case for identifiers / numbers / strings), the canonical
  // @c lexeme already matches the source and consumers should treat
  // @c SourceText() as equivalent to @c lexeme.  Lexers populate this field
  // only when the canonical form differs from the source form (for example
  // when normalizing case-insensitive keywords) so existing tests and
  // diagnostics keep their original wording.  Placed last so existing
  // 3- and 4-argument aggregate-initialization call sites remain
  // source-compatible.
  std::string raw_lexeme;

  /// Returns the original source spelling of this token: @c raw_lexeme when
  /// the lexer recorded a different source form, otherwise the canonical
  /// @c lexeme.  Use this for diagnostics and source-faithful formatters.
  const std::string &SourceText() const noexcept {
    return raw_lexeme.empty() ? lexeme : raw_lexeme;
  }
};

/** @brief LexerBase class. */
class LexerBase {
public:
  virtual ~LexerBase() = default;

  virtual Token NextToken() = 0;

  /// Optionally attach a shared token pool that will receive every token this
  /// lexer emits via @c EmitToken().  Pass @c nullptr to detach.  The pool
  /// must outlive the lexer.
  void SetTokenPool(SharedTokenPool *pool) noexcept { token_pool_ = pool; }

  /// Returns the currently attached pool (may be @c nullptr).
  SharedTokenPool *TokenPool() const noexcept { return token_pool_; }

protected:
  LexerBase(std::string source, std::string file) :
      source_(std::move(source)), file_(std::move(file)) {}

  /// Mirror @p t into the attached pool (if any) and return @p t unchanged.
  /// Lexer subclasses may call this from their @c NextToken() implementations
  /// to opt into shared-pool indexing without changing their return type.
  Token EmitToken(Token t);

  char Peek() const { return position_ < source_.size() ? source_[position_] : '\0'; }

  char PeekNext() const { return (position_ + 1) < source_.size() ? source_[position_ + 1] : '\0'; }

  char Get() {
    char c = Peek();
    if (c == '\n') {
      line_++;
      column_ = 1;
    } else if (c == '\t') {
      column_ += tab_width_;
    } else {
      column_++;
    }
    if (position_ < source_.size()) {
      position_++;
    }
    return c;
  }

  core::SourceLoc CurrentLoc() const { return core::SourceLoc{file_, line_, column_}; }

  bool Eof() const { return position_ >= source_.size(); }

  void SetTabWidth(size_t width) { tab_width_ = width; }

  // Save/restore lexer state for lookahead (public for parser access)
public:
  /** @brief LexerState data structure. */
  struct LexerState {
    size_t position;
    size_t line;
    size_t column;
  };

  LexerState SaveState() const { return {position_, line_, column_}; }

  void RestoreState(const LexerState &state) {
    position_ = state.position;
    line_ = state.line;
    column_ = state.column;
  }

protected:
  std::string source_;
  std::string file_;
  size_t position_{0};
  size_t line_{1};
  size_t column_{1};
  size_t tab_width_{4};
  SharedTokenPool *token_pool_{nullptr};
};

} // namespace polyglot::frontends
