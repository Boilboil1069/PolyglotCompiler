#include "runtime/include/gc/runtime.h"

#include <cstddef>

extern "C" {

void *polyglot_alloc(size_t size) {
  return polyglot::runtime::gc::GlobalHeap().Allocate(size);
}

void polyglot_gc_collect() {
  polyglot::runtime::gc::GlobalHeap().Collect();
}

void polyglot_gc_register_root(void **slot) {
  polyglot::runtime::gc::GlobalHeap().Raw()->RegisterRoot(slot);
}

void polyglot_gc_unregister_root(void **slot) {
  polyglot::runtime::gc::GlobalHeap().Raw()->UnregisterRoot(slot);
}

}
