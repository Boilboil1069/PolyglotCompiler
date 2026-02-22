#pragma once

#include <cstddef>

namespace polyglot::runtime::gc {

// GC statistics reported by collectors.
struct GCStats {
  size_t total_allocations{0};     // Total number of allocations since start.
  size_t total_bytes_allocated{0}; // Cumulative bytes allocated.
  size_t current_heap_bytes{0};    // Current live heap size in bytes.
  size_t peak_heap_bytes{0};       // Peak heap size observed.
  size_t collections{0};           // Number of GC cycles executed.
  size_t total_freed_bytes{0};     // Cumulative bytes freed.
  size_t live_objects{0};          // Current number of live objects.
  size_t root_count{0};            // Current number of registered roots.
};

class GC {
 public:
  virtual ~GC() = default;
  virtual void *Allocate(size_t size) = 0;
  virtual void Collect() = 0;
  virtual void RegisterRoot(void **slot) = 0;
  virtual void UnregisterRoot(void **slot) = 0;

  // Query runtime statistics from the collector.
  // The base implementation returns zeroed stats; concrete collectors
  // override this to provide real metrics.
  virtual GCStats GetStats() const { return GCStats{}; }
};

}  // namespace polyglot::runtime::gc
