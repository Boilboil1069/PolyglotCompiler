#pragma once

#include <string>

namespace polyglot::ir {

enum class IRTypeKind { kInvalid, kI1, kI32, kI64, kF32, kF64, kVoid };

struct IRType {
  IRTypeKind kind{IRTypeKind::kInvalid};
  std::string name;

  static IRType Invalid() { return IRType{IRTypeKind::kInvalid, "invalid"}; }
  static IRType I1() { return IRType{IRTypeKind::kI1, "i1"}; }
  static IRType I32() { return IRType{IRTypeKind::kI32, "i32"}; }
  static IRType I64() { return IRType{IRTypeKind::kI64, "i64"}; }
  static IRType F32() { return IRType{IRTypeKind::kF32, "f32"}; }
  static IRType F64() { return IRType{IRTypeKind::kF64, "f64"}; }
  static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }

  bool operator==(const IRType &other) const { return kind == other.kind && name == other.name; }
  bool operator!=(const IRType &other) const { return !(*this == other); }
};

}  // namespace polyglot::ir
