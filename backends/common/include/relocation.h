#pragma once

#include <string>

namespace polyglot::backends {

enum class RelocType { kAbs32, kAbs64, kPcRel32 };

struct Relocation {
  std::string symbol;
  RelocType type{RelocType::kAbs64};
  size_t offset{0};
};

}  // namespace polyglot::backends
