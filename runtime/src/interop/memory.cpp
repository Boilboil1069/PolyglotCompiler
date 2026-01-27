#include "runtime/include/interop/memory.h"

#include <atomic>
#include <utility>

namespace polyglot::runtime::interop {

ForeignObject *AcquireForeign(void *ptr, size_t size, std::function<void(void *)> deleter) {
  auto *obj = new ForeignObject();
  obj->ptr = ptr;
  obj->size = size;
  obj->deleter = std::move(deleter);
  obj->refcount.store(1);
  return obj;
}

void RetainForeign(ForeignObject *obj) {
  if (!obj) return;
  obj->refcount.fetch_add(1, std::memory_order_relaxed);
}

void ReleaseForeign(ForeignObject *obj) {
  if (!obj) return;
  if (obj->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    if (obj->deleter) obj->deleter(obj->ptr);
    delete obj;
  }
}

}  // namespace polyglot::runtime::interop
