#include "middle/include/ir/data_layout.h"

#include <algorithm>

namespace polyglot::ir {
namespace {
size_t AlignTo(size_t value, size_t align) {
  if (align == 0) return value;
  size_t rem = value % align;
  return rem ? value + (align - rem) : value;
}
}

size_t DataLayout::AlignOf(const IRType &type) const {
  switch (type.kind) {
    case IRTypeKind::kI1:
    case IRTypeKind::kI8:
      return 1;
    case IRTypeKind::kI16:
      return 2;
    case IRTypeKind::kI32:
    case IRTypeKind::kF32:
      return 4;
    case IRTypeKind::kI64:
    case IRTypeKind::kF64:
      return 8;
    case IRTypeKind::kPointer:
    case IRTypeKind::kReference:
      return PointerAlign();
    case IRTypeKind::kArray:
    case IRTypeKind::kVector: {
      if (type.subtypes.empty()) return 1;
      size_t elem_align = AlignOf(type.subtypes[0]);
      if (type.kind == IRTypeKind::kVector) {
        size_t lanes = type.count;
        size_t lane_size = SizeOf(type.subtypes[0]);
        size_t natural = lane_size * lanes;
        // For SIMD friendliness, enforce at least 16-byte alignment for 128-bit vectors.
        return std::max<size_t>(16, AlignTo(elem_align, natural));
      }
      return elem_align;
    }
    case IRTypeKind::kStruct: {
      size_t max_align = 1;
      for (auto &f : type.subtypes) {
        max_align = std::max(max_align, AlignOf(f));
      }
      return max_align == 0 ? 1 : max_align;
    }
    case IRTypeKind::kFunction:
      return 1;  // functions themselves are unsized; only pointers to them matter
    case IRTypeKind::kInvalid:
    case IRTypeKind::kVoid:
    default:
      return 1;
  }
}

size_t DataLayout::SizeOf(const IRType &type) const {
  switch (type.kind) {
    case IRTypeKind::kI1:
    case IRTypeKind::kI8:
      return 1;
    case IRTypeKind::kI16:
      return 2;
    case IRTypeKind::kI32:
    case IRTypeKind::kF32:
      return 4;
    case IRTypeKind::kI64:
    case IRTypeKind::kF64:
      return 8;
    case IRTypeKind::kPointer:
    case IRTypeKind::kReference:
      return PointerSize();
    case IRTypeKind::kArray: {
      if (type.subtypes.empty()) return 0;
      size_t elem_size = SizeOf(type.subtypes[0]);
      size_t elem_align = AlignOf(type.subtypes[0]);
      size_t stride = AlignTo(elem_size, elem_align);
      return stride * type.count;
    }
    case IRTypeKind::kVector: {
      if (type.subtypes.empty()) return 0;
      size_t elem_size = SizeOf(type.subtypes[0]);
      return elem_size * type.count;
    }
    case IRTypeKind::kStruct: {
      size_t offset = 0;
      size_t max_align = 1;
      for (auto &f : type.subtypes) {
        size_t align = AlignOf(f);
        size_t size = SizeOf(f);
        offset = AlignTo(offset, align);
        offset += size;
        max_align = std::max(max_align, align);
      }
      return AlignTo(offset, max_align == 0 ? 1 : max_align);
    }
    case IRTypeKind::kFunction:
      return 0;  // unsized
    case IRTypeKind::kInvalid:
    case IRTypeKind::kVoid:
    default:
      return 0;
  }
}

}  // namespace polyglot::ir
