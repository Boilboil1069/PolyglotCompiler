/**
 * @file     data_layout.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <cstddef>

#include "middle/include/ir/nodes/types.h"

namespace polyglot::ir {

/** @brief DataLayout class. */
class DataLayout {
 public:
  /** @brief Arch enumeration. */
  enum class Arch { kX86_64, kARM64 };

  explicit DataLayout(Arch arch) : arch_(arch) {}

  Arch Target() const { return arch_; }
  size_t PointerSize() const { return 8; }
  size_t PointerAlign() const { return 8; }

  size_t SizeOf(const IRType &type) const;
  size_t AlignOf(const IRType &type) const;

 private:
  Arch arch_;
};

}  // namespace polyglot::ir
