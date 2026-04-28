# Token Pool — Shared Frontend Lexeme & Identifier Interning

> Component note for `polyglot::frontends::TokenPool`.
> File version 1.2.1.  Author: Manning Cyrus.

## 1. Goal

Provide a single, canonical owner for token storage shared across every
language frontend, so that:

- Lexemes are stored once in a chunked byte arena (`StringArena`) and
  referenced by stable `std::string_view` values.
- Identifiers are deduplicated through an open-addressing hash table
  (`IdentifierTable`) producing dense 32-bit `SymbolId` values.
- Parsers can take a snapshot of the pool, do speculative lookahead, and
  rewind cleanly via `TokenPool::Save() / Restore()`.
- Concurrent UI indexing and parallel pipelines can share the same pool
  through `SharedTokenPool`, which guards every operation with a
  `std::shared_mutex`.

## 2. Public surface

Header: `frontends/common/include/token_pool.h`.

```cpp
namespace polyglot::frontends {

using TokenHandle = std::uint32_t;
inline constexpr TokenHandle kInvalidTokenHandle = 0xffffffffu;

struct TokenPoolStats {
  std::size_t tokens, arena_bytes, arena_capacity;
  std::size_t unique_identifiers, intern_hits, intern_misses;
};

class TokenPool {
public:
  explicit TokenPool(std::size_t arena_chunk_bytes = StringArena::kDefaultChunkBytes);
  TokenHandle      Add(Token token);
  const Token     &Get(TokenHandle handle) const;     // throws on invalid
  std::string_view InternLexeme(std::string_view text);
  SymbolId         InternIdentifier(std::string_view name);
  Token            MakeToken(TokenKind kind, std::string_view lex,
                             const core::SourceLoc &loc);

  struct Snapshot { /* tokens, arena mark, ident snapshot */ };
  Snapshot Save() const noexcept;
  void     Restore(const Snapshot &snap);
  void     Reset();          // alias: Clear()
  TokenPoolStats Stats() const noexcept;
};

class SharedTokenPool { /* same API, plus WithExclusive/WithShared(Fn) */ };

} // namespace polyglot::frontends
```

Building blocks:

| Header                                            | Purpose                                                        |
| ------------------------------------------------- | -------------------------------------------------------------- |
| `frontends/common/include/string_arena.h`         | Chunked monotonic byte arena (default 64 KiB chunks).          |
| `frontends/common/include/identifier_table.h`     | FNV-1a + open addressing, 0.75 load factor, dense `SymbolId`s. |
| `frontends/common/include/token_pool.h`           | Token storage + intern facade + thread-safe variant.           |

## 3. Driver / UI integration

- `FrontendOptions::token_pool` – optional `SharedTokenPool*` injected by
  callers.  `nullptr` keeps the legacy behaviour.
- `FrontendOptions::dump_token_pool_stats` – mirror of the new
  `--dump-token-pool` driver flag.
- `tools/polyc/src/stage_frontend.cpp` allocates a session-scoped
  `SharedTokenPool`, attaches it to the `Preprocessor` and (for `.ploy`)
  the `PloyLexer`, then captures `TokenPool::Stats()` into
  `FrontendResult::token_pool_stats_json`.
- `tools/polyc/src/driver.cpp` writes
  `<aux_dir>/<stem>.pool_stats.json` whenever `--dump-token-pool` is on.
- `tools/ui/common/src/compiler_service.cpp` attaches a per-call
  `SharedTokenPool` and surfaces its counters on `CompileResult::token_pool_stats`.
- `tools/ui/common/resources/default_settings.json` exposes three keys:
  `frontend.tokenPool.arenaChunkBytes`, `frontend.tokenPool.shared`,
  `frontend.tokenPool.dumpStats`.

## 4. Lexer opt-in protocol

`LexerBase` now carries an optional `SharedTokenPool*` and exposes:

```cpp
void   SetTokenPool(SharedTokenPool *pool) noexcept;
SharedTokenPool *TokenPool() const noexcept;
Token  EmitToken(Token t);  // protected helper: mirrors to pool, returns t
```

A frontend lexer may opt into shared-pool indexing by changing each
`return X;` site in `NextToken()` to `return EmitToken(X);`.  `Token::lexeme`
remains `std::string` so the migration is byte-compatible with all existing
consumers (parsers, AST nodes, syntax highlighter, ~30 test files).

`PloyLexer` is wired up through `stage_frontend`; the other 8 frontends can
adopt the helper file-by-file without coordinating breaking changes.

## 5. Tests

`tests/unit/frontend/token_pool_test.cpp` — 7 mandatory cases:

1. `StringArena` returns stable views across many chunks (1 000 strings +
   ~4 MiB additional pressure).
2. `StringArena::RewindTo` discards bytes appended after the mark.
3. `IdentifierTable` interns 100 000 unique + 10 000 duplicate names with
   correct hit/miss accounting and stable ids.
4. `TokenPool::Save/Restore` precisely rolls back tokens, arena bytes, and
   identifier ids after speculative lookahead.
5. `SharedTokenPool` 16-thread `InternIdentifier` campaign — every
   `shared_K` resolves to the same `SymbolId` across threads, total unique
   count matches the expected formula.
6. `TokenHandle` remains stable across 1 000 000 `Add()` calls; sampled
   handles round-trip byte-identical content.
7. `TokenPool::MakeToken` interns lexemes into the arena and is observable
   via `InternLexeme` lookup.

All seven assertions are behaviour-level; no `REQUIRE(true)`.

## 6. Performance characteristics

- Arena allocation: amortised O(1) per byte.  Chunks grow geometrically
  via the constructor parameter (clamped `[4 KiB, 16 MiB]`).
- Identifier intern: average O(1) probe (load factor ≤ 0.75).  Rehash on
  growth is proportional to the dense `symbols_` vector size.
- `SharedTokenPool` reads (`Get`, `Stats`, `Size`, `FindIdentifier`,
  `IdentifierName`) take a shared lock; mutations take an exclusive lock.

## 7. Design note: `Token::lexeme` retained as `std::string`

`Token::lexeme` keeps its `std::string` type rather than being retyped to
`std::string_view`, because (a) ~30 unit-test files compare lexemes
against literal strings, (b) `compiler_service.cpp` and
`syntax_highlighter.cpp` keep tokens by value across the pool's
lifetime, and (c) every frontend AST currently stores a copy of the
lexeme.  The arena/intern services are nevertheless fully exposed via
`InternLexeme` / `InternIdentifier` / `MakeToken`, so opt-in zero-copy
paths are available without breaking the existing API contract.

## 8. Dump file schema

`<stem>.pool_stats.json` (one object per compile invocation):

```json
{
  "language": "cpp",
  "tokens": 12345,
  "arena_bytes": 65536,
  "arena_capacity": 131072,
  "unique_identifiers": 678,
  "intern_hits": 9012,
  "intern_misses": 678
}
```
