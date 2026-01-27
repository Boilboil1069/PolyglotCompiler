#include "runtime/include/gc/runtime.h"

#include <mutex>

namespace polyglot::runtime::gc {
namespace {
Heap *g_heap = nullptr;
std::mutex g_heap_mu;
}

Heap &GlobalHeap() {
  if (g_heap) return *g_heap;
  std::lock_guard<std::mutex> lock(g_heap_mu);
  if (!g_heap) g_heap = new Heap();
  return *g_heap;
}

void SetGlobalGCStrategy(Strategy strategy) {
  std::lock_guard<std::mutex> lock(g_heap_mu);
  delete g_heap;
  g_heap = new Heap(strategy);
}

}  // namespace polyglot::runtime::gc
