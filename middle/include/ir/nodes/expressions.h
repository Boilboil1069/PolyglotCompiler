#pragma once

#include <memory>
#include <string>
#include <vector>

#include "middle/include/ir/nodes/types.h"

namespace polyglot::ir {

// Base value in the unified IR. Every SSA value inherits from this.
struct Value {
  virtual ~Value() = default;
  IRType type{IRType::Invalid()};
  std::string name;  // SSA name (may be empty pre-SSA)
};

// Literal (int or float).
struct LiteralExpression : Value {
  bool is_float{false};
  long long i64{0};
  double f64{0.0};

  explicit LiteralExpression(long long v = 0) : is_float(false), i64(v) { type = IRType::I64(); }
  explicit LiteralExpression(double v) : is_float(true), f64(v) { type = IRType::F64(); }
};

// Placeholder for uninitialized/undefined values.
struct UndefValue : Value {
  UndefValue() { type = IRType::Invalid(); name = "undef"; }
};

// Global or constant data placeholder.
struct GlobalValue : Value {
  bool is_const{false};
  std::string init;  // textual initializer for now
  std::shared_ptr<Value> initializer;  // structured initializer
};

struct ConstantString : Value {
  std::string data;
  bool null_terminated{true};
  explicit ConstantString(std::string d, bool nt = true)
      : data(std::move(d)), null_terminated(nt) {
    size_t len = data.size() + (null_terminated ? 1 : 0);
    type = IRType::Array(IRType::I8(), len);
  }
};

struct ConstantArray : Value {
  std::vector<std::shared_ptr<Value>> elements;
  ConstantArray(std::vector<std::shared_ptr<Value>> elems, IRType elem_type) : elements(std::move(elems)) {
    type = IRType::Array(elem_type, elements.size());
  }
};

struct ConstantStruct : Value {
  std::vector<std::shared_ptr<Value>> fields;
  explicit ConstantStruct(std::vector<std::shared_ptr<Value>> f, std::string name = "struct")
      : fields(std::move(f)) {
    std::vector<IRType> types;
    types.reserve(fields.size());
    for (auto &v : fields) types.push_back(v ? v->type : IRType::Invalid());
    type = IRType::Struct(std::move(name), types);
  }
};

// Constant address computation (like a getelementptr).
inline IRType ResolveGEPResultType(IRType base_ptr_type, const std::vector<size_t> &indices) {
  IRType cur = base_ptr_type;
  for (size_t idx : indices) {
    if (cur.kind == IRTypeKind::kPointer || cur.kind == IRTypeKind::kReference) {
      cur = cur.subtypes.empty() ? IRType::Invalid() : cur.subtypes[0];
      continue;
    }
    if (cur.kind == IRTypeKind::kArray || cur.kind == IRTypeKind::kVector) {
      cur = cur.subtypes.empty() ? IRType::Invalid() : cur.subtypes[0];
      continue;
    }
    if (cur.kind == IRTypeKind::kStruct) {
      cur = idx < cur.subtypes.size() ? cur.subtypes[idx] : IRType::Invalid();
      continue;
    }
  }
  return IRType::Pointer(cur);
}

struct ConstantGEP : Value {
  std::shared_ptr<Value> base;
  std::vector<size_t> indices;
  ConstantGEP(std::shared_ptr<Value> b, std::vector<size_t> idx)
      : base(std::move(b)), indices(std::move(idx)) {
    IRType base_ptr = base ? IRType::Pointer(base->type) : IRType::Pointer(IRType::Invalid());
    type = ResolveGEPResultType(base_ptr, indices);
  }
};

}  // namespace polyglot::ir
