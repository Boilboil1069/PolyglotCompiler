/**
 * @file     memory.cpp
 * @brief    Implementation of ForeignObject lifecycle management
 * @author   Manning Cyrus
 * @date     2026-02-06
 * @version  2.0.0
 */
#include "runtime/include/interop/memory.h"

#include <atomic>
#include <utility>

namespace polyglot::runtime::interop {

ForeignObject *AcquireForeign(void *ptr, size_t size, std::function<void(void *)> deleter,
                              Ownership ownership) {
  auto *obj = new ForeignObject();
  obj->ptr = ptr;
  obj->size = size;
  obj->deleter = std::move(deleter);
  obj->refcount.store(1, std::memory_order_relaxed);
  obj->ownership = ownership;
  return obj;
}

void RetainForeign(ForeignObject *obj) {
  if (!obj) return;
  obj->refcount.fetch_add(1, std::memory_order_relaxed);
}

void ReleaseForeign(ForeignObject *obj) {
  if (!obj) return;
  if (obj->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    // Call deleter for owned objects.  Shared objects also invoke the deleter
    // on final release (they share the cleanup responsibility).
    if (obj->deleter &&
        (obj->ownership == Ownership::kOwned || obj->ownership == Ownership::kShared)) {
      obj->deleter(obj->ptr);
    }
    delete obj;
  }
}

size_t ForeignRefCount(const ForeignObject *obj) {
  if (!obj) return 0;
  return obj->refcount.load(std::memory_order_relaxed);
}

bool ForeignIsAlive(const ForeignObject *obj) {
  return ForeignRefCount(obj) > 0;
}

}  // namespace polyglot::runtime::interop
