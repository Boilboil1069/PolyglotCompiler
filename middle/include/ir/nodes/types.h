#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace polyglot::ir {

enum class IRTypeKind {
  kInvalid,
  kI1,
  kI8,
  kI16,
  kI32,
  kI64,
  kF32,
  kF64,
  kVoid,
  kPointer,
  kReference,
  kArray,
  kVector,
  kStruct,
  kFunction
};

struct IRType {
  IRTypeKind kind{IRTypeKind::kInvalid};
  std::string name;
  std::vector<IRType> subtypes;  // pointee/element/fields/params+ret (ret first for function)
  size_t count{0};               // array length or vector lanes
  bool is_signed{true};          // only meaningful for integers

  static IRType Invalid() { return IRType{IRTypeKind::kInvalid, "invalid"}; }
  static IRType I1() { IRType t{IRTypeKind::kI1, "i1"}; t.is_signed = false; return t; }
  static IRType I8(bool is_signed = true) { IRType t{IRTypeKind::kI8, is_signed ? "i8" : "u8"}; t.is_signed = is_signed; return t; }
  static IRType I16(bool is_signed = true) { IRType t{IRTypeKind::kI16, is_signed ? "i16" : "u16"}; t.is_signed = is_signed; return t; }
  static IRType I32(bool is_signed = true) { IRType t{IRTypeKind::kI32, is_signed ? "i32" : "u32"}; t.is_signed = is_signed; return t; }
  static IRType I64(bool is_signed = true) { IRType t{IRTypeKind::kI64, is_signed ? "i64" : "u64"}; t.is_signed = is_signed; return t; }
  static IRType F32() { return IRType{IRTypeKind::kF32, "f32"}; }
  static IRType F64() { return IRType{IRTypeKind::kF64, "f64"}; }
  static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }

  static IRType Pointer(const IRType &pointee) {
    IRType t{IRTypeKind::kPointer, pointee.name + "*"};
    t.subtypes.push_back(pointee);
    return t;
  }

  static IRType Reference(const IRType &pointee) {
    IRType t{IRTypeKind::kReference, pointee.name + "&"};
    t.subtypes.push_back(pointee);
    return t;
  }

  static IRType Array(const IRType &elem, size_t n) {
    IRType t{IRTypeKind::kArray, elem.name + "[" + std::to_string(n) + "]"};
    t.subtypes.push_back(elem);
    t.count = n;
    return t;
  }

  static IRType Vector(const IRType &elem, size_t lanes) {
    IRType t{IRTypeKind::kVector, "<" + std::to_string(lanes) + " x " + elem.name + ">"};
    t.subtypes.push_back(elem);
    t.count = lanes;
    return t;
  }

  static IRType Struct(std::string name, std::vector<IRType> fields) {
    IRType t{IRTypeKind::kStruct, std::move(name)};
    t.subtypes = std::move(fields);
    t.count = t.subtypes.size();
    return t;
  }

  static IRType Function(const IRType &ret, const std::vector<IRType> &params) {
    IRType t{IRTypeKind::kFunction, "fn"};
    t.subtypes.reserve(params.size() + 1);
    t.subtypes.push_back(ret);
    t.subtypes.insert(t.subtypes.end(), params.begin(), params.end());
    t.count = params.size();
    return t;
  }

  // Return a copy with explicit signedness (only for integers).
  IRType WithSigned(bool sign) const {
    IRType t = *this;
    t.is_signed = sign;
    if (t.IsInteger()) {
      if (t.kind == IRTypeKind::kI8) t.name = sign ? "i8" : "u8";
      if (t.kind == IRTypeKind::kI16) t.name = sign ? "i16" : "u16";
      if (t.kind == IRTypeKind::kI32) t.name = sign ? "i32" : "u32";
      if (t.kind == IRTypeKind::kI64) t.name = sign ? "i64" : "u64";
    }
    return t;
  }

  bool IsInteger() const { return kind == IRTypeKind::kI1 || kind == IRTypeKind::kI8 || kind == IRTypeKind::kI16 || kind == IRTypeKind::kI32 || kind == IRTypeKind::kI64; }
  bool IsFloat() const { return kind == IRTypeKind::kF32 || kind == IRTypeKind::kF64; }
  bool IsScalar() const { return IsInteger() || IsFloat(); }

  bool IsSigned() const { return IsInteger() && is_signed; }

  int BitWidth() const {
    switch (kind) {
      case IRTypeKind::kI1: return 1;
      case IRTypeKind::kI8: return 8;
      case IRTypeKind::kI16: return 16;
      case IRTypeKind::kI32: return 32;
      case IRTypeKind::kI64: return 64;
      case IRTypeKind::kF32: return 32;
      case IRTypeKind::kF64: return 64;
      default: return 0;
    }
  }

  // Structural equality (name included for structs). Uses subtypes/count for composites.
  bool SameShape(const IRType &other) const {
    if (kind != other.kind || count != other.count) return false;
    if (IsInteger() && is_signed != other.is_signed) return false;
    if (kind == IRTypeKind::kStruct && name != other.name) return false;
    if (subtypes.size() != other.subtypes.size()) return false;
    for (size_t i = 0; i < subtypes.size(); ++i) {
      if (!subtypes[i].SameShape(other.subtypes[i])) return false;
    }
    return true;
  }

  // Simple lossless conversion: integer/float widening, pointer/ref rebind to same pointee shape.
  bool CanLosslesslyConvertTo(const IRType &dst) const {
    if (SameShape(dst)) return true;
    if (IsInteger() && dst.IsInteger()) {
      int rank_src = BitWidth();
      int rank_dst = dst.BitWidth();
      if (is_signed != dst.is_signed && rank_src == rank_dst) return false;
      return rank_src <= rank_dst;
    }
    if (IsFloat() && dst.IsFloat()) {
      int rank_src = (kind == IRTypeKind::kF32) ? 32 : 64;
      int rank_dst = (dst.kind == IRTypeKind::kF32) ? 32 : 64;
      return rank_src <= rank_dst;
    }
    if ((kind == IRTypeKind::kPointer || kind == IRTypeKind::kReference) &&
        (dst.kind == IRTypeKind::kPointer || dst.kind == IRTypeKind::kReference)) {
      if (subtypes.empty() || dst.subtypes.empty()) return false;
      return subtypes[0].SameShape(dst.subtypes[0]);
    }
    return false;
  }

  // Bitcast-compatible: same size/shape for pointers/refs/vectors; structural for arrays/structs.
  bool CanBitcastTo(const IRType &dst) const {
    if (SameShape(dst)) return true;
    if ((kind == IRTypeKind::kPointer || kind == IRTypeKind::kReference) &&
        (dst.kind == IRTypeKind::kPointer || dst.kind == IRTypeKind::kReference)) {
      return true;  // allow any ptr<->ptr/reference bitcast
    }
    if (kind == IRTypeKind::kVector && dst.kind == IRTypeKind::kVector && count == dst.count) {
      return true;
    }
    if (kind == IRTypeKind::kArray && dst.kind == IRTypeKind::kArray && count == dst.count) {
      return subtypes.empty() || dst.subtypes.empty() || subtypes[0].CanBitcastTo(dst.subtypes[0]);
    }
    if (kind == IRTypeKind::kStruct && dst.kind == IRTypeKind::kStruct && subtypes.size() == dst.subtypes.size()) {
      return true;  // structural size equality assumed
    }
    return false;
  }

  bool operator==(const IRType &other) const {
    if (kind != other.kind || name != other.name || count != other.count) return false;
    if (IsInteger() && is_signed != other.is_signed) return false;
    if (subtypes.size() != other.subtypes.size()) return false;
    for (size_t i = 0; i < subtypes.size(); ++i) {
      if (!(subtypes[i] == other.subtypes[i])) return false;
    }
    return true;
  }
  bool operator!=(const IRType &other) const { return !(*this == other); }
};

}  // namespace polyglot::ir
