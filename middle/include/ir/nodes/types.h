#pragma once

#include <string>

namespace polyglot::ir {

enum class IRTypeKind { kInvalid, kI32, kI64, kF32, kF64, kVoid };

struct IRType {
  IRTypeKind kind{IRTypeKind::kInvalid};
  std::string name;

  static IRType I32() { return IRType{IRTypeKind::kI32, "i32"}; }
  static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }
};

}  // namespace polyglot::ir
