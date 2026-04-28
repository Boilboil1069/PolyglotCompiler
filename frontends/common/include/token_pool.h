/**
 * @file     token_pool.h
 * @brief    Shared frontend token storage with arena-backed lexemes,
 *           identifier interning, snapshot/restore and a thread-safe variant.
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 *
 * The TokenPool is the canonical owner of token storage shared by every
 * language frontend.  It provides three orthogonal services:
 *
 *  - Stable token storage via a deque-backed sequence whose references are
 *    valid until the next @c Reset() / @c Restore() call.  @c TokenHandle is
 *    a 32-bit dense id used to refer to a token across vector reallocations.
 *  - Lexeme interning into a backing @c StringArena, so callers may obtain
 *    @c std::string_view for any token text without lifetime concerns.
 *  - Identifier deduplication via @c IdentifierTable yielding compact
 *    @c SymbolId values, enabling zero-copy keyword matching and scope
 *    lookup.
 *
 * In addition to the single-threaded @c TokenPool, @c SharedTokenPool wraps
 * the same operations behind a @c std::shared_mutex for concurrent UI
 * indexing and parallel front-end pipelines.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>

#include "frontends/common/include/identifier_table.h"
#include "frontends/common/include/lexer_base.h"
#include "frontends/common/include/string_arena.h"

namespace polyglot::frontends {

/// Stable handle into a TokenPool.  Always usable until @c Reset() or a
/// @c Restore() that rolls back past the handle's index.
using TokenHandle                                  = std::uint32_t;
inline constexpr TokenHandle kInvalidTokenHandle = 0xffffffffu;

/**
 * @brief Aggregated counters describing a TokenPool's state.
 */
struct TokenPoolStats {
  std::size_t tokens{0};
  std::size_t arena_bytes{0};
  std::size_t arena_capacity{0};
  std::size_t unique_identifiers{0};
  std::size_t intern_hits{0};
  std::size_t intern_misses{0};
};

/**
 * @brief Single-threaded token pool.
 *
 * Tokens are stored in an @c std::deque so push_back never invalidates
 * existing references / handles.  Lexemes are also copied into an internal
 * @c StringArena so callers can obtain stable @c std::string_view bytes via
 * @c InternLexeme() / @c MakeToken() without owning a string copy.
 */
class TokenPool {
public:
  explicit TokenPool(std::size_t arena_chunk_bytes = StringArena::kDefaultChunkBytes);
  TokenPool(const TokenPool &)                = delete;
  TokenPool &operator=(const TokenPool &)     = delete;
  TokenPool(TokenPool &&) noexcept            = default;
  TokenPool &operator=(TokenPool &&) noexcept = default;
  ~TokenPool()                                = default;

  /** @name Token storage */
  /** @{ */

  /// Append @p token and return a stable handle.  The token's lexeme bytes
  /// are also interned into the arena so a stable @c std::string_view is
  /// available via @c InternLexeme() lookups.
  TokenHandle Add(Token token);

  /// Reference to the token at @p handle.  Throws @c std::out_of_range if the
  /// handle does not refer to a live token.
  const Token &Get(TokenHandle handle) const;

  /// Number of tokens currently stored.
  std::size_t Size() const noexcept { return tokens_.size(); }

  /// All tokens in insertion order.  The deque guarantees iterator stability.
  const std::deque<Token> &All() const noexcept { return tokens_; }

  /** @} */

  /** @name String / identifier interning */
  /** @{ */

  /// Intern @p text into the arena and return a stable view.
  std::string_view InternLexeme(std::string_view text);

  /// Intern @p name into the identifier table and return its dense id.
  SymbolId InternIdentifier(std::string_view name);

  /// Look up an existing identifier without inserting; returns
  /// @c kInvalidSymbolId when @p name has never been interned.
  SymbolId FindIdentifier(std::string_view name) const;

  /// Recover the bytes of a previously interned identifier.
  std::string_view IdentifierName(SymbolId id) const;

  /// Build a token whose lexeme bytes are stably interned into the arena.
  /// Convenience helper used by lexers.
  Token MakeToken(TokenKind kind, std::string_view lexeme,
                  const core::SourceLoc &loc);

  /** @} */

  /** @name Snapshot / restore for parser lookahead */
  /** @{ */

  struct Snapshot {
    std::size_t               token_count{0};
    StringArena::Mark         arena_mark{};
    IdentifierTable::Snapshot identifier_snapshot{};
  };

  /// Capture the current pool state.
  Snapshot Save() const noexcept;

  /// Roll back to a previously captured snapshot.  Tokens, arena bytes, and
  /// identifier ids appended since are dropped.  TokenHandles obtained after
  /// the snapshot must be considered invalid.
  void Restore(const Snapshot &snap);

  /** @} */

  /** @name Lifecycle / observability */
  /** @{ */

  /// Drop every token, arena byte, and identifier id.  Underlying chunks are
  /// released; the next allocation starts fresh.
  void Reset();

  /// Backwards-compatible alias for @c Reset().
  void Clear() { Reset(); }

  /// Snapshot of pool counters useful for diagnostics and benchmarks.
  TokenPoolStats Stats() const noexcept;

  /// Direct access to the underlying arena (read-only).
  const StringArena &Arena() const noexcept { return *arena_; }

  /// Direct access to the underlying identifier table.
  const IdentifierTable &Identifiers() const noexcept { return *identifiers_; }

  /** @} */

private:
  // Heap-allocated so the pool is move-constructible without invalidating
  // the IdentifierTable's reference to the arena.
  std::unique_ptr<StringArena>     arena_;
  std::unique_ptr<IdentifierTable> identifiers_;
  std::deque<Token>                tokens_{};
};

/**
 * @brief Thread-safe wrapper around @c TokenPool.
 *
 * Reads (Get / Stats / Size) take a shared lock; mutations take an exclusive
 * lock.  @c Get() returns by value so callers do not retain a lock.
 */
class SharedTokenPool {
public:
  explicit SharedTokenPool(std::size_t arena_chunk_bytes = StringArena::kDefaultChunkBytes);
  SharedTokenPool(const SharedTokenPool &)            = delete;
  SharedTokenPool &operator=(const SharedTokenPool &) = delete;

  TokenHandle Add(Token token);
  Token       Get(TokenHandle handle) const;
  std::size_t Size() const noexcept;

  std::string_view InternLexeme(std::string_view text);
  SymbolId         InternIdentifier(std::string_view name);
  SymbolId         FindIdentifier(std::string_view name) const;
  std::string_view IdentifierName(SymbolId id) const;
  Token            MakeToken(TokenKind kind, std::string_view lexeme,
                             const core::SourceLoc &loc);

  TokenPoolStats Stats() const noexcept;
  void           Reset();

  /// Run @p fn while holding the pool's exclusive lock.  Useful to perform
  /// composite operations atomically (e.g. intern several strings together).
  template <typename Fn>
  auto WithExclusive(Fn &&fn) -> decltype(fn(std::declval<TokenPool &>())) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return fn(inner_);
  }

  /// Run @p fn while holding the pool's shared lock.
  template <typename Fn>
  auto WithShared(Fn &&fn) const -> decltype(fn(std::declval<const TokenPool &>())) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return fn(inner_);
  }

private:
  mutable std::shared_mutex mutex_{};
  TokenPool                 inner_;
};

} // namespace polyglot::frontends
