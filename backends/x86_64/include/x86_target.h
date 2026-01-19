#pragma once

#include "backends/common/include/target_machine.h"

namespace polyglot::backends::x86_64 {

class X86Target : public TargetMachine {
 public:
  std::string TargetTriple() const override { return "x86_64-unknown-elf"; }
  std::string EmitAssembly() override;
};

}  // namespace polyglot::backends::x86_64
