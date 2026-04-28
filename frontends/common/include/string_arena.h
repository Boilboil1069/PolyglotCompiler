/**
 * @file     string_arena.h
 * @brief    Monotonic chunked byte arena for stable string interning.
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 *
 * StringArena owns a chain of monotonically growing byte chunks.  Strings
 * interned through @c Intern() receive a @c std::string_view whose backing
 * bytes remain valid for the entire lifetime of the arena (or until @c Reset()
 * is called).  Existing chunks are never reallocated or moved; only fresh
 * chunks are appended at the tail.  This guarantees that any
 * @c std::string_view previously returned remains valid as long as no
 * destructive operation is performed.
 *
 * The arena is intentionally allocator-light: it acquires aligned blocks via
 * @c operator new[] and releases them in @c Reset() / destructor only.  No
 * per-string allocations occur.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace polyglot::frontends {

/**
 * @brief Chunked byte arena providing stable interned string storage.
 *
 * Thread safety: not thread safe.  See @c SharedTokenPool for a synchronized
 * variant of the higher level pool.
 */
class StringArena {
public:
  /// Default chunk size in bytes (64 KiB) when caller does not request another.
  static constexpr std::size_t kDefaultChunkBytes = 64u * 1024u;
  /// Lower bound enforced when configuring chunk size (4 KiB).
  static constexpr std::size_t kMinChunkBytes = 4u * 1024u;
  /// Upper bound enforced when configuring chunk size (16 MiB).
  static constexpr std::size_t kMaxChunkBytes = 16u * 1024u * 1024u;

  /**
   * @brief Construct an arena with the given default chunk size.
   * @param chunk_bytes  Requested chunk size.  Clamped into
   *                     [@c kMinChunkBytes, @c kMaxChunkBytes].
   */
  explicit StringArena(std::size_t chunk_bytes = kDefaultChunkBytes);
  StringArena(const StringArena &)            = delete;
  StringArena &operator=(const StringArena &) = delete;
  StringArena(StringArena &&) noexcept;
  StringArena &operator=(StringArena &&) noexcept;
  ~StringArena();

  /**
   * @brief Copy @p s into the arena and return a stable view.
   *
   * The returned view's bytes are NUL-padded for legacy callers that may pass
   * the @c data() pointer to a C interface, but @c size() does not include the
   * trailing NUL.  Empty inputs return a view with @c size() == 0 and a
   * non-null @c data() pointer.
   */
  std::string_view Intern(std::string_view s);

  /**
   * @brief Position marker used by @c RewindTo() to undo recent allocations.
   *
   * The marker is opaque to callers; it captures the arena's logical write
   * cursor at the time @c Mark() was called.  Rewinding to a stale marker
   * (i.e. one captured before a @c Reset()) yields a fresh empty arena.
   */
  struct Mark {
    std::size_t chunk_index{0};
    std::size_t chunk_offset{0};
  };

  /// Capture the current write cursor for later rewind.
  Mark CurrentMark() const noexcept;

  /**
   * @brief Logically reset the write cursor to a previously captured @p mark.
   *
   * Chunks allocated after the mark are kept (so subsequent allocations may
   * reuse them) but their memory is considered invalidated; any
   * @c std::string_view obtained after @p mark must no longer be dereferenced.
   */
  void RewindTo(const Mark &mark) noexcept;

  /// Total number of bytes currently occupied by interned strings.
  std::size_t BytesUsed() const noexcept { return bytes_used_; }

  /// Total bytes reserved across all chunks (including unused tail capacity).
  std::size_t Capacity() const noexcept { return capacity_; }

  /// Number of underlying chunks currently allocated.
  std::size_t ChunkCount() const noexcept { return chunks_.size(); }

  /**
   * @brief Discard all interned data.  All previously returned views become
   *        invalid.  Underlying chunk allocations are released.
   */
  void Reset() noexcept;

  /// Configured default chunk size (post-clamping).
  std::size_t DefaultChunkBytes() const noexcept { return chunk_bytes_; }

private:
  struct Chunk {
    std::unique_ptr<char[]> data;
    std::size_t              capacity{0};
    std::size_t              used{0};
  };

  /// Ensure at least @p needed bytes can be appended; allocate a new chunk if
  /// the active chunk lacks the room.
  void EnsureRoom(std::size_t needed);

  std::size_t        chunk_bytes_{kDefaultChunkBytes};
  std::vector<Chunk> chunks_{};
  std::size_t        active_index_{0};
  std::size_t        bytes_used_{0};
  std::size_t        capacity_{0};
};

} // namespace polyglot::frontends
