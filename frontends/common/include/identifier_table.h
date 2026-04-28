/**
 * @file     identifier_table.h
 * @brief    Open-addressing string interning table producing dense SymbolIds.
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 *
 * IdentifierTable canonicalises identifier strings to dense 32-bit ids.  The
 * mapping is stable for the life of the table: once an id is assigned to a
 * string, it never changes (until @c Reset() is called).  Lookup is O(1)
 * average.  Identifier bytes themselves are stored in an associated
 * @c StringArena so the table itself only carries hashes and indices.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "frontends/common/include/string_arena.h"

namespace polyglot::frontends {

/// Dense identifier identity.  @c kInvalidSymbolId marks an absent value.
using SymbolId                              = std::uint32_t;
inline constexpr SymbolId kInvalidSymbolId = 0xffffffffu;

/**
 * @brief Identifier interning table backed by an external StringArena.
 *
 * Thread safety: not thread safe.  Concurrent access must be wrapped (see
 * @c SharedTokenPool).
 */
class IdentifierTable {
public:
  /**
   * @brief Construct a table that interns into @p arena.
   * @param arena  Backing arena; must outlive this table.
   */
  explicit IdentifierTable(StringArena &arena);

  /**
   * @brief Intern @p name and return its dense id.  Subsequent calls with the
   *        same byte sequence return the same id.
   */
  SymbolId Intern(std::string_view name);

  /**
   * @brief Look up an existing id without inserting.
   * @return @c kInvalidSymbolId when the name has never been interned.
   */
  SymbolId Find(std::string_view name) const;

  /// Recover the bytes for a previously interned id.
  std::string_view Lookup(SymbolId id) const;

  /// Number of unique identifiers currently held.
  std::size_t Size() const noexcept { return symbols_.size(); }

  /// Number of @c Intern() calls that found an existing id.
  std::size_t HitCount() const noexcept { return hit_count_; }

  /// Number of @c Intern() calls that allocated a new id.
  std::size_t MissCount() const noexcept { return miss_count_; }

  /// Snapshot used for transactional rollback (e.g. parser lookahead).
  struct Snapshot {
    std::size_t symbol_count{0};
    std::size_t hit_count{0};
    std::size_t miss_count{0};
  };

  Snapshot CurrentSnapshot() const noexcept;
  /**
   * @brief Roll back the table to a previously captured snapshot.
   *
   * Symbols allocated after the snapshot are removed and their hash slots
   * cleared.  The hash table is rebuilt to maintain probing correctness.
   */
  void RestoreSnapshot(const Snapshot &snap);

  /// Drop every id and rebuild the hash table.  All previously returned
  /// SymbolIds become invalid.
  void Reset();

private:
  struct Slot {
    std::uint32_t hash{0};
    SymbolId      id{kInvalidSymbolId};
  };

  static constexpr std::size_t kInitialBuckets = 64;
  static constexpr double      kMaxLoadFactor  = 0.75;

  std::uint32_t HashOf(std::string_view s) const noexcept;
  void          GrowIfNeeded();
  void          Rehash(std::size_t new_bucket_count);
  std::size_t   ProbeFor(std::string_view s, std::uint32_t hash, bool &found) const noexcept;

  StringArena                 &arena_;
  std::vector<Slot>            buckets_{};
  std::vector<std::string_view> symbols_{}; // dense table indexed by SymbolId
  std::size_t                  hit_count_{0};
  std::size_t                  miss_count_{0};
};

} // namespace polyglot::frontends
