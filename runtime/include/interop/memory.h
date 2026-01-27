#pragma once

#include <atomic>
#include <cstddef>
#include <functional>

namespace polyglot::runtime::interop {

struct ForeignObject {
  void *ptr{nullptr};
  size_t size{0};
  std::function<void(void *)> deleter;
  std::atomic<size_t> refcount{1};
};

ForeignObject *AcquireForeign(void *ptr, size_t size, std::function<void(void *)> deleter);
void RetainForeign(ForeignObject *obj);
void ReleaseForeign(ForeignObject *obj);

}  // namespace polyglot::runtime::interop
