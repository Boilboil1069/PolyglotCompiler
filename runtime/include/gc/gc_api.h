#pragma once

#include <cstddef>

namespace polyglot::runtime::gc {

class GC {
 public:
  virtual ~GC() = default;
  virtual void *Allocate(size_t size) = 0;
  virtual void Collect() = 0;
};

}  // namespace polyglot::runtime::gc
