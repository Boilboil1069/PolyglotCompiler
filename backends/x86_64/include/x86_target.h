#pragma once

#include "backends/common/include/target_machine.h"
#include "backends/x86_64/include/machine_ir.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::backends::x86_64 {

class X86Target : public TargetMachine {
 public:
  explicit X86Target(const polyglot::ir::IRContext *module = nullptr) : module_(module) {}

  void SetModule(const polyglot::ir::IRContext *module) { module_ = module; }
  void SetRegAllocStrategy(RegAllocStrategy strategy) { regalloc_strategy_ = strategy; }

  std::string TargetTriple() const override { return "x86_64-unknown-elf"; }
  std::string EmitAssembly() override;

 private:
  const polyglot::ir::IRContext *module_{nullptr};
  RegAllocStrategy regalloc_strategy_{RegAllocStrategy::kLinearScan};
};

}  // namespace polyglot::backends::x86_64
