#pragma once

#include <memory>

#include "runtime/include/gc/gc_api.h"

namespace polyglot::runtime::gc {

enum class Strategy { kMarkSweep, kGenerational };

std::unique_ptr<GC> MakeGC(Strategy strategy);

}  // namespace polyglot::runtime::gc
