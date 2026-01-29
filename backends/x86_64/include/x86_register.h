#pragma once

#include <string>

namespace polyglot::backends::x86_64 {

enum class Register {
  kRax,
  kRbx,
  kRcx,
  kRdx,
  kRsp,
  kRbp,
  kRsi,
  kRdi,
  kR8,
  kR9,
  kR10,
  kR11,
  kR12,
  kR13,
  kR14,
  kR15
};

inline std::string RegisterName(Register reg) {
  switch (reg) {
    case Register::kRax:
      return "rax";
    case Register::kRbx:
      return "rbx";
    case Register::kRcx:
      return "rcx";
    case Register::kRdx:
      return "rdx";
    case Register::kRsp:
      return "rsp";
    case Register::kRbp:
      return "rbp";
    case Register::kRsi:
      return "rsi";
    case Register::kRdi:
      return "rdi";
    case Register::kR8:
      return "r8";
    case Register::kR9:
      return "r9";
    case Register::kR10:
      return "r10";
    case Register::kR11:
      return "r11";
    case Register::kR12:
      return "r12";
    case Register::kR13:
      return "r13";
    case Register::kR14:
      return "r14";
    case Register::kR15:
      return "r15";
  }
  return "";
}

}  // namespace polyglot::backends::x86_64
