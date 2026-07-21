/**
 * @file     binary_inspector.cpp
 * @brief    Binary container detection and disassembler facade.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/viewer/binary_inspector.h"

#include <sstream>
#include <iomanip>

namespace polyglot::tools::ui::viewer {

std::string BinaryKindName(BinaryKind k) {
  switch (k) {
    case BinaryKind::kElf:   return "elf";
    case BinaryKind::kPe:    return "pe";
    case BinaryKind::kMachO: return "mach-o";
    case BinaryKind::kWasm:  return "wasm";
    case BinaryKind::kUnknown: return "unknown";
  }
  return "unknown";
}

namespace {

uint16_t ReadU16LE(const std::vector<uint8_t> &b, size_t off) {
  if (off + 1 >= b.size()) return 0;
  return static_cast<uint16_t>(b[off]) |
         (static_cast<uint16_t>(b[off + 1]) << 8);
}

uint32_t ReadU32LE(const std::vector<uint8_t> &b, size_t off) {
  if (off + 3 >= b.size()) return 0;
  return  static_cast<uint32_t>(b[off]) |
         (static_cast<uint32_t>(b[off + 1]) << 8) |
         (static_cast<uint32_t>(b[off + 2]) << 16) |
         (static_cast<uint32_t>(b[off + 3]) << 24);
}

std::string ElfArch(uint16_t machine) {
  switch (machine) {
    case 0x03: return "x86";
    case 0x3E: return "x86_64";
    case 0x28: return "arm";
    case 0xB7: return "aarch64";
    case 0xF3: return "riscv";
    default:   return "unknown";
  }
}

std::string PeArch(uint16_t machine) {
  switch (machine) {
    case 0x014C: return "x86";
    case 0x8664: return "x86_64";
    case 0x01C0: return "arm";
    case 0xAA64: return "aarch64";
    default:     return "unknown";
  }
}

}  // namespace

BinaryInfo IdentifyBinary(const std::vector<uint8_t> &b) {
  BinaryInfo info;
  if (b.size() >= 4 && b[0] == 0x7F && b[1] == 'E' &&
      b[2] == 'L' && b[3] == 'F') {
    info.kind = BinaryKind::kElf;
    info.bits = (b.size() > 4 && b[4] == 2) ? 64 : 32;
    info.little_endian = !(b.size() > 5 && b[5] == 2);
    uint16_t etype = ReadU16LE(b, 16);
    switch (etype) {
      case 1: info.subsystem = "relocatable"; break;
      case 2: info.subsystem = "executable"; break;
      case 3: info.subsystem = "shared-object"; break;
      case 4: info.subsystem = "core"; break;
      default: info.subsystem = "unknown"; break;
    }
    info.arch = ElfArch(ReadU16LE(b, 18));
    return info;
  }
  if (b.size() >= 64 && b[0] == 'M' && b[1] == 'Z') {
    uint32_t pe_off = ReadU32LE(b, 0x3C);
    if (pe_off + 6 <= b.size() && b[pe_off] == 'P' && b[pe_off + 1] == 'E' &&
        b[pe_off + 2] == 0 && b[pe_off + 3] == 0) {
      info.kind = BinaryKind::kPe;
      uint16_t machine = ReadU16LE(b, pe_off + 4);
      info.arch = PeArch(machine);
      info.bits = (machine == 0x8664 || machine == 0xAA64) ? 64 : 32;
      info.subsystem = "executable";
      return info;
    }
  }
  if (b.size() >= 4) {
    uint32_t magic = ReadU32LE(b, 0);
    if (magic == 0xFEEDFACE || magic == 0xFEEDFACF ||
        magic == 0xCEFAEDFE || magic == 0xCFFAEDFE) {
      info.kind = BinaryKind::kMachO;
      info.bits = (magic == 0xFEEDFACF || magic == 0xCFFAEDFE) ? 64 : 32;
      info.little_endian = (magic == 0xFEEDFACE || magic == 0xFEEDFACF);
      uint32_t cpu = ReadU32LE(b, 4);
      switch (cpu) {
        case 0x01000007: info.arch = "x86_64"; break;
        case 0x0100000C: info.arch = "aarch64"; break;
        case 0x00000007: info.arch = "x86"; break;
        case 0x0000000C: info.arch = "arm"; break;
        default:         info.arch = "unknown"; break;
      }
      info.subsystem = "mach-o";
      return info;
    }
  }
  if (b.size() >= 8 &&
      b[0] == 0x00 && b[1] == 'a' && b[2] == 's' && b[3] == 'm') {
    info.kind = BinaryKind::kWasm;
    info.bits = 32;
    info.arch = "wasm32";
    info.subsystem = "module";
    return info;
  }
  return info;
}

std::vector<DisassembledInstruction> DisassemblerFacade::Disassemble(
    const std::vector<uint8_t> &bytes, uint64_t base) const {
  std::vector<DisassembledInstruction> out;
  // Placeholder rendering: one row per byte with hex literal.
  // The IDE binds against polyasm in the production build; this
  // facade keeps the panel populated for unsupported targets.
  for (size_t i = 0; i < bytes.size(); ++i) {
    DisassembledInstruction ins;
    ins.address = base + i;
    ins.raw = {bytes[i]};
    std::ostringstream oss;
    oss << ".byte 0x" << std::hex << std::setw(2)
        << std::setfill('0') << static_cast<int>(bytes[i]);
    ins.mnemonic = ".byte";
    ins.operands = oss.str();
    out.push_back(std::move(ins));
  }
  return out;
}

}  // namespace polyglot::tools::ui::viewer
