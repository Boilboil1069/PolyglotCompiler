/**
 * @file     string_arena.cpp
 * @brief    Implementation of the chunked string arena.
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "frontends/common/include/string_arena.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace polyglot::frontends {

namespace {
// Sentinel byte returned for empty interns so callers always observe a
// non-null data() pointer.  Reading the byte yields '\0'.
const char kEmptySentinel[1] = {'\0'};
} // namespace

StringArena::StringArena(std::size_t chunk_bytes) {
  chunk_bytes_ = std::clamp(chunk_bytes, kMinChunkBytes, kMaxChunkBytes);
}

StringArena::StringArena(StringArena &&other) noexcept :
    chunk_bytes_(other.chunk_bytes_),
    chunks_(std::move(other.chunks_)),
    active_index_(other.active_index_),
    bytes_used_(other.bytes_used_),
    capacity_(other.capacity_) {
  other.active_index_ = 0;
  other.bytes_used_   = 0;
  other.capacity_     = 0;
}

StringArena &StringArena::operator=(StringArena &&other) noexcept {
  if (this != &other) {
    chunks_.clear();
    chunk_bytes_        = other.chunk_bytes_;
    chunks_             = std::move(other.chunks_);
    active_index_       = other.active_index_;
    bytes_used_         = other.bytes_used_;
    capacity_           = other.capacity_;
    other.active_index_ = 0;
    other.bytes_used_   = 0;
    other.capacity_     = 0;
  }
  return *this;
}

StringArena::~StringArena() = default;

void StringArena::EnsureRoom(std::size_t needed) {
  // Probe the active chunk first.
  if (!chunks_.empty()) {
    Chunk &active = chunks_[active_index_];
    if (active.capacity - active.used >= needed) {
      return;
    }
  }
  // Walk forward looking for an existing chunk with capacity (after a rewind).
  for (std::size_t i = active_index_ + 1; i < chunks_.size(); ++i) {
    if (chunks_[i].capacity - chunks_[i].used >= needed) {
      active_index_ = i;
      return;
    }
  }
  // Allocate a fresh chunk.  For oversize requests, the chunk grows to fit.
  const std::size_t new_capacity = std::max(chunk_bytes_, needed);
  Chunk             chunk;
  chunk.data     = std::make_unique<char[]>(new_capacity);
  chunk.capacity = new_capacity;
  chunk.used     = 0;
  chunks_.push_back(std::move(chunk));
  active_index_ = chunks_.size() - 1;
  capacity_ += new_capacity;
}

std::string_view StringArena::Intern(std::string_view s) {
  if (s.empty()) {
    return std::string_view{kEmptySentinel, 0};
  }
  // Reserve room for the bytes plus a trailing NUL for C-API compatibility.
  EnsureRoom(s.size() + 1);
  Chunk &active = chunks_[active_index_];
  char  *dst    = active.data.get() + active.used;
  std::memcpy(dst, s.data(), s.size());
  dst[s.size()] = '\0';
  active.used += s.size() + 1;
  bytes_used_ += s.size() + 1;
  return std::string_view{dst, s.size()};
}

StringArena::Mark StringArena::CurrentMark() const noexcept {
  if (chunks_.empty()) {
    return Mark{0, 0};
  }
  return Mark{active_index_, chunks_[active_index_].used};
}

void StringArena::RewindTo(const Mark &mark) noexcept {
  if (mark.chunk_index >= chunks_.size()) {
    // Stale mark: best-effort reset to empty state without releasing chunks.
    for (Chunk &c : chunks_) {
      bytes_used_ -= c.used;
      c.used = 0;
    }
    active_index_ = 0;
    bytes_used_   = 0;
    return;
  }
  // Trim trailing chunks back to their used=0 state.
  for (std::size_t i = mark.chunk_index + 1; i < chunks_.size(); ++i) {
    bytes_used_ -= chunks_[i].used;
    chunks_[i].used = 0;
  }
  Chunk &target = chunks_[mark.chunk_index];
  if (mark.chunk_offset <= target.used) {
    bytes_used_ -= (target.used - mark.chunk_offset);
    target.used = mark.chunk_offset;
  }
  active_index_ = mark.chunk_index;
}

void StringArena::Reset() noexcept {
  chunks_.clear();
  active_index_ = 0;
  bytes_used_   = 0;
  capacity_     = 0;
}

} // namespace polyglot::frontends
