#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "middle/include/ir/nodes/expressions.h"

namespace polyglot::ir {

struct BasicBlock;  // fwd

// Base class for all IR instructions (also a value when producing results).
struct Instruction : Value {
  BasicBlock *parent{nullptr};
  std::vector<std::string> operands;  // variable/use names; constants use empty names

  virtual bool IsTerminator() const { return false; }
  bool HasResult() const { return !name.empty() && type.kind != IRTypeKind::kVoid; }
};

struct BinaryInstruction : Instruction {
  enum class Op { kAdd, kSub, kMul, kDiv, kAnd, kOr, kCmpEq, kCmpLt };
  Op op{Op::kAdd};
  BinaryInstruction() { type = IRType::I64(); }
};

struct PhiInstruction : Instruction {
  std::vector<std::pair<BasicBlock *, std::string>> incomings;  // (pred, varName)
};

// Pre-SSA assignment of a named variable to a value name.
struct AssignInstruction : Instruction {
  AssignInstruction() { type = IRType::Invalid(); }
};

struct CallInstruction : Instruction {
  std::string callee;
  bool is_indirect{false};  // if true, callee is an operand name
};

struct AllocaInstruction : Instruction {
  AllocaInstruction() { type = IRType::Pointer(IRType::Invalid()); }
};

struct LoadInstruction : Instruction {
  LoadInstruction() { type = IRType::Invalid(); }
};

struct StoreInstruction : Instruction {
  StoreInstruction() { type = IRType::Void(); }
};

struct CastInstruction : Instruction {
  enum class CastKind { kZExt, kSExt, kTrunc, kBitcast };
  CastKind cast{CastKind::kBitcast};
};

struct GetElementPtrInstruction : Instruction {
  IRType source_type{IRType::Invalid()};
  std::vector<size_t> indices;
  GetElementPtrInstruction() { type = IRType::Pointer(IRType::Invalid()); }
};

struct ReturnStatement : Instruction {
  ReturnStatement() { type = IRType::Void(); }
  bool IsTerminator() const override { return true; }
};

struct BranchStatement : Instruction {
  BasicBlock *target{nullptr};
  BranchStatement() { type = IRType::Void(); }
  bool IsTerminator() const override { return true; }
};

struct CondBranchStatement : Instruction {
  BasicBlock *true_target{nullptr};
  BasicBlock *false_target{nullptr};
  CondBranchStatement() { type = IRType::Void(); }
  bool IsTerminator() const override { return true; }
};

struct SwitchStatement : Instruction {
  struct Case {
    long long value;
    BasicBlock *target{nullptr};
  };
  std::vector<Case> cases;
  BasicBlock *default_target{nullptr};
  SwitchStatement() { type = IRType::Void(); }
  bool IsTerminator() const override { return true; }
};

using Statement = Instruction;  // backward compatibility

}  // namespace polyglot::ir
