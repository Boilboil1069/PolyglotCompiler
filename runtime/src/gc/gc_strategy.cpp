#include "runtime/include/gc/gc_strategy.h"

#include <memory>

namespace polyglot::runtime::gc {
namespace {
std::unique_ptr<GC> MakeMarkSweepGC();
std::unique_ptr<GC> MakeGenerationalGC();
}  // namespace

std::unique_ptr<GC> MakeGC(Strategy strategy) {
  switch (strategy) {
    case Strategy::kGenerational:
      return MakeGenerationalGC();
    case Strategy::kMarkSweep:
    default:
      return MakeMarkSweepGC();
  }
}

}  // namespace polyglot::runtime::gc
