#pragma once

#include <atomic>
#include <cstddef>
#include <functional>

namespace polyglot::runtime::interop {

enum class Ownership { kBorrowed, kOwned, kShared };

struct ForeignObject {
  void *ptr{nullptr};
  size_t size{0};
  std::function<void(void *)> deleter;
  std::atomic<size_t> refcount{1};
  Ownership ownership{Ownership::kOwned};
};

ForeignObject *AcquireForeign(void *ptr, size_t size, std::function<void(void *)> deleter,
                              Ownership ownership = Ownership::kOwned);
void RetainForeign(ForeignObject *obj);
void ReleaseForeign(ForeignObject *obj);

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
