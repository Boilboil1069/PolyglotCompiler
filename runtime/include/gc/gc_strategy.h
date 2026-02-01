#pragma once

#include <memory>

#include "runtime/include/gc/gc_api.h"

namespace polyglot::runtime::gc {

enum class Strategy { 
  kMarkSweep,      // 标记-清除GC
  kGenerational,   // 分代GC
  kCopying,        // 复制式GC
  kIncremental     // 增量式GC
};

std::unique_ptr<GC> MakeGC(Strategy strategy);

}  // namespace polyglot::runtime::gc
