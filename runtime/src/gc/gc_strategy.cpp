#include "runtime/include/gc/gc_strategy.h"

#include <memory>

namespace polyglot::runtime::gc {

std::unique_ptr<GC> MakeMarkSweepGC();
std::unique_ptr<GC> MakeGenerationalGC();
std::unique_ptr<GC> MakeCopyingGC();
std::unique_ptr<GC> MakeIncrementalGC();

std::unique_ptr<GC> MakeGC(Strategy strategy) {
  switch (strategy) {
    case Strategy::kGenerational:
      return MakeGenerationalGC();
    case Strategy::kCopying:
      return MakeCopyingGC();
    case Strategy::kIncremental:
      return MakeIncrementalGC();
    case Strategy::kMarkSweep:
    default:
      return MakeMarkSweepGC();
  }
}

}  // namespace polyglot::runtime::gc
