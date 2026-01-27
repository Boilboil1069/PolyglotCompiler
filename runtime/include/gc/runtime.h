#pragma once

#include "runtime/include/gc/heap.h"

namespace polyglot::runtime::gc {

// Returns a process-wide heap instance used by runtime helpers.
Heap &GlobalHeap();

// Swap the global heap strategy (intended for testing/configuration).
void SetGlobalGCStrategy(Strategy strategy);

}  // namespace polyglot::runtime::gc
