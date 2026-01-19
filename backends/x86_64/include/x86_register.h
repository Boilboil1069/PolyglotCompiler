#pragma once

#include <string>

namespace polyglot::backends::x86_64 {

enum class Register { kRax, kRbx, kRcx, kRdx };

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
  }
  return "";
}

}  // namespace polyglot::backends::x86_64
