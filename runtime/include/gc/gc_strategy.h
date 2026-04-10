/**
 * @file     gc_strategy.h
 * @brief    Garbage collection subsystem
 *
 * @ingroup  Runtime / GC
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>

#include "runtime/include/gc/gc_api.h"

namespace polyglot::runtime::gc {

/** @brief Strategy enumeration. */
enum class Strategy { 
  kMarkSweep,      // 标记-清除GC
  kGenerational,   // 分代GC
  kCopying,        // 复制式GC
  kIncremental     // 增量式GC
};

std::unique_ptr<GC> MakeGC(Strategy strategy);

}  // namespace polyglot::runtime::gc
