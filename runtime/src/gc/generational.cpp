#include "runtime/include/gc/gc_api.h"

#include <cstdlib>

namespace polyglot::runtime::gc {

class GenerationalGC : public GC {
 public:
  void *Allocate(size_t size) override { return std::malloc(size); }
  void Collect() override {}
};

}  // namespace polyglot::runtime::gc
