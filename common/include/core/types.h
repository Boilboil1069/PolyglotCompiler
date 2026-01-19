#pragma once

#include <memory>
#include <string>
#include <utility>

namespace polyglot::core {

enum class TypeKind {
  kInvalid,
  kVoid,
  kBool,
  kInt,
  kFloat,
  kString,
  kPointer,
  kFunction
};

struct Type {
  TypeKind kind{TypeKind::kInvalid};
  std::string name;

  static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
  static Type Void() { return Type{TypeKind::kVoid, "void"}; }
  static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
  static Type Int() { return Type{TypeKind::kInt, "int"}; }
  static Type Float() { return Type{TypeKind::kFloat, "float"}; }
  static Type String() { return Type{TypeKind::kString, "string"}; }

  bool operator==(const Type &other) const {
    return kind == other.kind && name == other.name;
  }
};

class TypeSystem {
 public:
  TypeSystem() = default;

  Type PointerTo(Type element) const {
    return Type{TypeKind::kPointer, element.name + "*"};
  }

  Type FunctionType(const std::string &signature) const {
    return Type{TypeKind::kFunction, signature};
  }
};

}  // namespace polyglot::core
