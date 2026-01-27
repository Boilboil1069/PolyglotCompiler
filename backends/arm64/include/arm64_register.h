#pragma once

#include <string>

namespace polyglot::backends::arm64 {

// General-purpose registers for AArch64 (X0-X30, SP treated specially).
enum class Register {
  kX0, kX1, kX2, kX3, kX4, kX5, kX6, kX7,
  kX8, kX9, kX10, kX11, kX12, kX13, kX14, kX15,
  kX16, kX17, kX18, kX19, kX20, kX21, kX22, kX23,
  kX24, kX25, kX26, kX27, kX28, kFp, kLr, kSp
};

inline std::string RegisterName(Register reg) {
  switch (reg) {
    case Register::kX0: return "x0";
    case Register::kX1: return "x1";
    case Register::kX2: return "x2";
    case Register::kX3: return "x3";
    case Register::kX4: return "x4";
    case Register::kX5: return "x5";
    case Register::kX6: return "x6";
    case Register::kX7: return "x7";
    case Register::kX8: return "x8";
    case Register::kX9: return "x9";
    case Register::kX10: return "x10";
    case Register::kX11: return "x11";
    case Register::kX12: return "x12";
    case Register::kX13: return "x13";
    case Register::kX14: return "x14";
    case Register::kX15: return "x15";
    case Register::kX16: return "x16";
    case Register::kX17: return "x17";
    case Register::kX18: return "x18";
    case Register::kX19: return "x19";
    case Register::kX20: return "x20";
    case Register::kX21: return "x21";
    case Register::kX22: return "x22";
    case Register::kX23: return "x23";
    case Register::kX24: return "x24";
    case Register::kX25: return "x25";
    case Register::kX26: return "x26";
    case Register::kX27: return "x27";
    case Register::kX28: return "x28";
    case Register::kFp: return "x29";
    case Register::kLr: return "x30";
    case Register::kSp: return "sp";
  }
  return "";
}

inline bool IsCallerSaved(Register reg) {
  switch (reg) {
    case Register::kX0: case Register::kX1: case Register::kX2: case Register::kX3:
    case Register::kX4: case Register::kX5: case Register::kX6: case Register::kX7:
    case Register::kX8: case Register::kX9: case Register::kX10: case Register::kX11:
    case Register::kX12: case Register::kX13: case Register::kX14: case Register::kX15:
    case Register::kX16: case Register::kX17:
      return true;
    default:
      return false;
  }
}

}  // namespace polyglot::backends::arm64
