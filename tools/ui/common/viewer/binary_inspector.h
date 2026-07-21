/**
 * @file     binary_inspector.h
 * @brief    Binary container detection (ELF / PE / Mach-O / WASM)
 *           and a thin disassembler facade that delegates to the
 *           polyasm shared core.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace polyglot::tools::ui::viewer {

enum class BinaryKind {
  kUnknown,
  kElf,
  kPe,
  kMachO,
  kWasm,
};

std::string BinaryKindName(BinaryKind k);

struct BinaryInfo {
  BinaryKind kind{BinaryKind::kUnknown};
  std::string arch;          ///< "x86_64" / "aarch64" / "wasm32" / ...
  std::string subsystem;     ///< "executable" / "shared-object" / "dll" / ...
  bool little_endian{true};
  uint16_t bits{0};          ///< 32 / 64.
};

BinaryInfo IdentifyBinary(const std::vector<uint8_t> &bytes);

struct DisassembledInstruction {
  uint64_t address{0};
  std::vector<uint8_t> raw;
  std::string mnemonic;
  std::string operands;
};

/// Thin facade: the production build links against the polyasm
/// disassembler.  This value-model layer exposes a tiny demo
/// table so the IDE can render placeholder rows for
/// architectures that polyasm does not yet support.
class DisassemblerFacade {
 public:
  std::vector<DisassembledInstruction> Disassemble(
      const std::vector<uint8_t> &bytes,
      uint64_t base_address = 0) const;
};

}  // namespace polyglot::tools::ui::viewer
