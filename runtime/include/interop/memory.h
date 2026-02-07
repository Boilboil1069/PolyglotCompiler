/**
 * @file     memory.h
 * @brief    Foreign object memory management with ownership semantics
 * @details  Provides reference-counted ForeignObject wrappers with three ownership
 *           modes: Borrowed (no cleanup), Owned (exclusive, deleter called on last release),
 *           and Shared (reference-counted, deleter called when refcount drops to zero).
 *
 * @author   Manning Cyrus
 * @date     2026-02-06
 * @version  2.0.0
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace polyglot::runtime::interop {

/// Ownership mode for foreign resources.
enum class Ownership { kBorrowed, kOwned, kShared };

/// Returns a human-readable label for an ownership mode.
inline const char *OwnershipName(Ownership o) {
  switch (o) {
    case Ownership::kBorrowed: return "borrowed";
    case Ownership::kOwned:    return "owned";
    case Ownership::kShared:   return "shared";
  }
  return "unknown";
}

/// A reference-counted wrapper around a foreign (non-GC) memory region.
/// The deleter is invoked when the last reference to an *owned* or *shared*
/// object is released.
struct ForeignObject {
  void *ptr{nullptr};
  size_t size{0};
  std::function<void(void *)> deleter;
  std::atomic<size_t> refcount{1};
  Ownership ownership{Ownership::kOwned};
  std::string tag;  ///< Optional diagnostic tag (e.g. originating language).
};

/// Create a new ForeignObject on the heap.
ForeignObject *AcquireForeign(void *ptr, size_t size, std::function<void(void *)> deleter,
                              Ownership ownership = Ownership::kOwned);

/// Increment the reference count.
void RetainForeign(ForeignObject *obj);

/// Decrement the reference count.  Calls the deleter and frees the wrapper
/// when the count reaches zero (for owned objects) or for shared objects.
void ReleaseForeign(ForeignObject *obj);

/// Query the current reference count (mainly for diagnostics / testing).
size_t ForeignRefCount(const ForeignObject *obj);

/// Check whether the object is still alive (refcount > 0).
bool ForeignIsAlive(const ForeignObject *obj);

inline ForeignObject *AcquireBorrowed(void *ptr, size_t size) {
  return AcquireForeign(ptr, size, nullptr, Ownership::kBorrowed);
}

inline ForeignObject *AcquireOwned(void *ptr, size_t size, std::function<void(void *)> deleter) {
  return AcquireForeign(ptr, size, std::move(deleter), Ownership::kOwned);
}

inline ForeignObject *AcquireShared(void *ptr, size_t size, std::function<void(void *)> deleter) {
  return AcquireForeign(ptr, size, std::move(deleter), Ownership::kShared);
}

}  // namespace polyglot::runtime::interop
