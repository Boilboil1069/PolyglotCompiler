/**
 * @file     identifier_table.cpp
 * @brief    Implementation of the open-addressing identifier table.
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "frontends/common/include/identifier_table.h"

#include <algorithm>
#include <cassert>

namespace polyglot::frontends {

IdentifierTable::IdentifierTable(StringArena &arena) : arena_(arena) {
  buckets_.assign(kInitialBuckets, Slot{});
}

std::uint32_t IdentifierTable::HashOf(std::string_view s) const noexcept {
  // FNV-1a 32-bit; sufficient quality for identifier-length inputs.
  std::uint32_t h = 0x811c9dc5u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 0x01000193u;
  }
  // Avoid the sentinel value 0 so we can use it as "empty" in slots.
  return h == 0 ? 0xffffffffu : h;
}

std::size_t IdentifierTable::ProbeFor(std::string_view s, std::uint32_t hash,
                                      bool &found) const noexcept {
  const std::size_t mask = buckets_.size() - 1;
  std::size_t       idx  = hash & mask;
  for (;;) {
    const Slot &slot = buckets_[idx];
    if (slot.id == kInvalidSymbolId) {
      found = false;
      return idx;
    }
    if (slot.hash == hash && symbols_[slot.id] == s) {
      found = true;
      return idx;
    }
    idx = (idx + 1) & mask;
  }
}

void IdentifierTable::GrowIfNeeded() {
  const double load = static_cast<double>(symbols_.size() + 1) /
                      static_cast<double>(buckets_.size());
  if (load > kMaxLoadFactor) {
    Rehash(buckets_.size() * 2);
  }
}

void IdentifierTable::Rehash(std::size_t new_bucket_count) {
  // Round up to the next power of two so we can use mask-based indexing.
  std::size_t pow2 = 1;
  while (pow2 < new_bucket_count) {
    pow2 <<= 1;
  }
  std::vector<Slot> new_buckets(pow2);
  const std::size_t mask = pow2 - 1;
  for (SymbolId id = 0; id < symbols_.size(); ++id) {
    const std::uint32_t h   = HashOf(symbols_[id]);
    std::size_t         idx = h & mask;
    while (new_buckets[idx].id != kInvalidSymbolId) {
      idx = (idx + 1) & mask;
    }
    new_buckets[idx].id   = id;
    new_buckets[idx].hash = h;
  }
  buckets_.swap(new_buckets);
}

SymbolId IdentifierTable::Intern(std::string_view name) {
  GrowIfNeeded();
  const std::uint32_t h     = HashOf(name);
  bool                found = false;
  const std::size_t   idx   = ProbeFor(name, h, found);
  if (found) {
    ++hit_count_;
    return buckets_[idx].id;
  }
  // Miss path: copy bytes into the arena and assign a fresh id.
  ++miss_count_;
  const std::string_view stored = arena_.Intern(name);
  const SymbolId         id     = static_cast<SymbolId>(symbols_.size());
  symbols_.push_back(stored);
  buckets_[idx].id   = id;
  buckets_[idx].hash = h;
  return id;
}

SymbolId IdentifierTable::Find(std::string_view name) const {
  if (buckets_.empty()) {
    return kInvalidSymbolId;
  }
  const std::uint32_t h     = HashOf(name);
  bool                found = false;
  const std::size_t   idx   = ProbeFor(name, h, found);
  return found ? buckets_[idx].id : kInvalidSymbolId;
}

std::string_view IdentifierTable::Lookup(SymbolId id) const {
  if (id >= symbols_.size()) {
    return {};
  }
  return symbols_[id];
}

IdentifierTable::Snapshot IdentifierTable::CurrentSnapshot() const noexcept {
  Snapshot s;
  s.symbol_count = symbols_.size();
  s.hit_count    = hit_count_;
  s.miss_count   = miss_count_;
  return s;
}

void IdentifierTable::RestoreSnapshot(const Snapshot &snap) {
  if (snap.symbol_count > symbols_.size()) {
    return; // forward restore is meaningless; no-op
  }
  symbols_.resize(snap.symbol_count);
  hit_count_  = snap.hit_count;
  miss_count_ = snap.miss_count;
  // Hash table must be rebuilt because we cannot identify which slots were
  // owned by removed ids without an auxiliary structure.
  std::fill(buckets_.begin(), buckets_.end(), Slot{});
  Rehash(buckets_.size());
}

void IdentifierTable::Reset() {
  symbols_.clear();
  std::fill(buckets_.begin(), buckets_.end(), Slot{});
  if (buckets_.size() != kInitialBuckets) {
    buckets_.assign(kInitialBuckets, Slot{});
  }
  hit_count_  = 0;
  miss_count_ = 0;
}

} // namespace polyglot::frontends
