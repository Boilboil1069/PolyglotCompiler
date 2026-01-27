#pragma once

#include <string>

namespace polyglot::backends::x86_64 {

enum class Register { kRax, kRbx, kRcx, kRdx, kRsp, kRbp, kRsi, kRdi };

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
  }
  return "";
}

}  // namespace polyglot::backends::x86_64
