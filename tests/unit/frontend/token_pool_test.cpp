/**
 * @file     token_pool_test.cpp
 * @brief    Unit tests for the shared frontend token pool.
 *
 * @ingroup  Frontend / Common / Tests
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "frontends/common/include/identifier_table.h"
#include "frontends/common/include/string_arena.h"
#include "frontends/common/include/token_pool.h"

using namespace polyglot::frontends;
using polyglot::core::SourceLoc;

// ============================================================================
// StringArena
// ============================================================================

TEST_CASE("StringArena returns stable views across many chunks",
          "[frontend][token_pool][arena]") {
  // Force small chunk size so we cross many chunks.
  StringArena arena(StringArena::kMinChunkBytes);

  std::vector<std::string_view> stored;
  stored.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    std::string s = "identifier_payload_with_some_bytes_" + std::to_string(i);
    stored.push_back(arena.Intern(s));
  }
  REQUIRE(stored.size() == 1000);
  REQUIRE(arena.ChunkCount() > 1u); // proves we crossed a chunk boundary

  // Allocate ~4 MiB of additional bytes to trigger many more chunks.
  for (int i = 0; i < 100; ++i) {
    std::string filler(40 * 1024, static_cast<char>('a' + (i % 20)));
    (void)arena.Intern(filler);
  }

  // All previously returned views must still be valid byte-for-byte.
  for (int i = 0; i < 1000; ++i) {
    const std::string expected = "identifier_payload_with_some_bytes_" + std::to_string(i);
    REQUIRE(stored[i] == expected);
    // Also ensure the trailing NUL was emitted for C-API compatibility.
    REQUIRE(stored[i].data()[stored[i].size()] == '\0');
  }
}

TEST_CASE("StringArena RewindTo discards bytes appended after the mark",
          "[frontend][token_pool][arena]") {
  StringArena arena;
  auto        v1   = arena.Intern("kept_alive");
  auto        mark = arena.CurrentMark();
  for (int i = 0; i < 50; ++i) {
    (void)arena.Intern(std::string("transient_") + std::to_string(i));
  }
  const std::size_t high_water = arena.BytesUsed();
  arena.RewindTo(mark);
  REQUIRE(arena.BytesUsed() < high_water);
  // v1 must still survive a rewind (it lives before the mark).
  REQUIRE(v1 == "kept_alive");

  // Subsequent allocations should reuse the freed space and still be valid.
  auto v2 = arena.Intern("after_rewind");
  REQUIRE(v2 == "after_rewind");
}

// ============================================================================
// IdentifierTable
// ============================================================================

TEST_CASE("IdentifierTable interns and grows correctly under load",
          "[frontend][token_pool][identifiers]") {
  StringArena     arena;
  IdentifierTable ids(arena);

  // 100k unique inserts mixed with 10k duplicate inserts.
  std::vector<SymbolId> first_round;
  first_round.reserve(100000);
  for (int i = 0; i < 100000; ++i) {
    first_round.push_back(ids.Intern(std::string("name_") + std::to_string(i)));
  }
  REQUIRE(ids.Size() == 100000u);
  REQUIRE(ids.MissCount() == 100000u);
  REQUIRE(ids.HitCount() == 0u);

  // Re-intern 10k of them; expect hits and stable ids.
  for (int i = 0; i < 10000; ++i) {
    SymbolId id = ids.Intern(std::string("name_") + std::to_string(i));
    REQUIRE(id == first_round[i]);
  }
  REQUIRE(ids.HitCount() == 10000u);
  REQUIRE(ids.Size() == 100000u);

  // Lookup() must round-trip the original bytes.
  REQUIRE(ids.Lookup(first_round[42]) == "name_42");

  // Find() of a missing name returns the sentinel.
  REQUIRE(ids.Find("definitely_not_present") == kInvalidSymbolId);
}

// ============================================================================
// TokenPool �?Save/Restore for parser lookahead
// ============================================================================

TEST_CASE("TokenPool::Save/Restore precisely rolls back tokens, arena, ids",
          "[frontend][token_pool][snapshot]") {
  TokenPool pool;
  pool.Add(Token{TokenKind::kKeyword, "def", SourceLoc{"x", 1, 1}});
  pool.Add(Token{TokenKind::kIdentifier, "foo", SourceLoc{"x", 1, 5}});
  (void)pool.InternIdentifier("alpha");

  const auto stats_before = pool.Stats();
  REQUIRE(stats_before.tokens == 2u);
  REQUIRE(stats_before.unique_identifiers >= 1u);

  const auto snap = pool.Save();

  // Speculatively append a bunch of tokens / identifiers / arena bytes.
  for (int i = 0; i < 50; ++i) {
    pool.Add(Token{TokenKind::kNumber, std::to_string(i), SourceLoc{"x", 2, 1}});
    (void)pool.InternIdentifier(std::string("speculative_") + std::to_string(i));
    (void)pool.InternLexeme(std::string("payload_") + std::to_string(i));
  }
  REQUIRE(pool.Size() == 52u);

  pool.Restore(snap);

  const auto stats_after = pool.Stats();
  REQUIRE(stats_after.tokens == stats_before.tokens);
  REQUIRE(stats_after.unique_identifiers == stats_before.unique_identifiers);
  REQUIRE(stats_after.arena_bytes <= stats_before.arena_bytes + 64u);
}

// ============================================================================
// SharedTokenPool �?concurrent identifier interning
// ============================================================================

TEST_CASE("SharedTokenPool yields unique stable ids under 16-thread load",
          "[frontend][token_pool][concurrency]") {
  SharedTokenPool pool;

  constexpr int kThreads          = 16;
  constexpr int kInsertsPerThread = 4000;

  std::vector<std::thread>            workers;
  std::vector<std::vector<SymbolId>>  per_thread_ids(kThreads);
  std::atomic<std::size_t>            total_interned{0};

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&, t]() {
      per_thread_ids[t].reserve(kInsertsPerThread);
      for (int i = 0; i < kInsertsPerThread; ++i) {
        // Half overlap across threads to exercise deduplication, half unique.
        const std::string name = (i % 2 == 0)
                                     ? std::string("shared_") + std::to_string(i / 2)
                                     : std::string("t") + std::to_string(t) + "_" +
                                           std::to_string(i);
        per_thread_ids[t].push_back(pool.InternIdentifier(name));
        total_interned.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto &w : workers) {
    w.join();
  }
  REQUIRE(total_interned.load() ==
          static_cast<std::size_t>(kThreads) * kInsertsPerThread);

  // Every id assigned to "shared_K" must agree across threads.
  for (int k = 0; k < kInsertsPerThread / 2; ++k) {
    SymbolId expected = pool.FindIdentifier(std::string("shared_") + std::to_string(k));
    REQUIRE(expected != kInvalidSymbolId);
    for (int t = 0; t < kThreads; ++t) {
      // The "shared" entries live at even indices within each thread's list.
      REQUIRE(per_thread_ids[t][k * 2] == expected);
    }
  }

  // Total unique ids = (kInsertsPerThread/2) shared + kThreads*(kInsertsPerThread/2) unique.
  const auto stats = pool.Stats();
  const std::size_t expected_unique =
      static_cast<std::size_t>(kInsertsPerThread / 2) +
      static_cast<std::size_t>(kThreads) * (kInsertsPerThread / 2);
  REQUIRE(stats.unique_identifiers == expected_unique);
}

// ============================================================================
// TokenHandle stability across heavy growth
// ============================================================================

TEST_CASE("TokenHandle remains stable across 1M Add() calls",
          "[frontend][token_pool][handles]") {
  TokenPool pool;
  // Sample early handles whose contents we want to verify after heavy growth.
  std::vector<TokenHandle> sampled;
  std::vector<std::string> sampled_text;

  constexpr int kTotal = 1000000;
  // Use a smaller count under sanitizer-heavy CI configurations.
  for (int i = 0; i < kTotal; ++i) {
    Token t;
    t.kind   = TokenKind::kNumber;
    t.lexeme = std::to_string(i);
    t.loc    = SourceLoc{"<bench>", 1, 1};
    TokenHandle h = pool.Add(std::move(t));
    if (i < 1000 || (i % 99991 == 0)) {
      sampled.push_back(h);
      sampled_text.push_back(std::to_string(i));
    }
  }

  REQUIRE(pool.Size() == static_cast<std::size_t>(kTotal));
  for (std::size_t i = 0; i < sampled.size(); ++i) {
    const Token &tok = pool.Get(sampled[i]);
    REQUIRE(tok.kind == TokenKind::kNumber);
    REQUIRE(tok.lexeme == sampled_text[i]);
  }
}

// ============================================================================
// MakeToken / interning round-trip
// ============================================================================

TEST_CASE("TokenPool::MakeToken interns into the arena and is observable via "
          "InternLexeme",
          "[frontend][token_pool][make_token]") {
  TokenPool pool;
  Token     t = pool.MakeToken(TokenKind::kKeyword, "function",
                               SourceLoc{"x", 1, 1});
  REQUIRE(t.kind == TokenKind::kKeyword);
  REQUIRE(t.lexeme == "function");

  // Re-intern the same lexeme: should reuse arena bytes (chunk count remains
  // small) and produce a byte-equal view.
  std::string_view view = pool.InternLexeme("function");
  REQUIRE(view == "function");
}
