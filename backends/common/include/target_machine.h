/**
 * @file     target_machine.h
 * @brief    Shared backend infrastructure
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>

namespace polyglot::backends {

/** @brief TargetMachine class. */
class TargetMachine {
 public:
  virtual ~TargetMachine() = default;
  virtual std::string TargetTriple() const = 0;
  virtual std::string EmitAssembly() = 0;
};

}  // namespace polyglot::backends
