#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

  struct MCReloc {
    std::string section;
    std::uint32_t offset{0};
    std::uint32_t type{1};   // 0 abs64, 1 rel32
    std::string symbol;      // target symbol name
    std::int64_t addend{0};
  };

  struct MCSymbol {
    std::string name;
    std::string section;
    std::uint64_t value{0};
    std::uint64_t size{0};
    bool global{true};
    bool defined{false};
  };

  struct MCSection {
    std::string name;
    std::vector<std::uint8_t> data;
    bool bss{false};
  };

  struct MCResult {
    std::vector<MCSection> sections;
    std::vector<MCReloc> relocs;
    std::vector<MCSymbol> symbols;
  };

  // Emit machine code with relocation info. Returns empty text on failure.
  MCResult EmitObjectCode();

 private:
  const polyglot::ir::IRContext *module_{nullptr};
  RegAllocStrategy regalloc_strategy_{RegAllocStrategy::kLinearScan};
};

}  // namespace polyglot::backends::x86_64
