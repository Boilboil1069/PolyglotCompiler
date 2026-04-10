/**
 * @file     relocation.h
 * @brief    Shared backend infrastructure
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>

namespace polyglot::backends {

/** @brief RelocType enumeration. */
enum class RelocType { kAbs32, kAbs64, kPcRel32 };

/** @brief Relocation data structure. */
struct Relocation {
  std::string symbol;
  RelocType type{RelocType::kAbs64};
  size_t offset{0};
};

}  // namespace polyglot::backends
