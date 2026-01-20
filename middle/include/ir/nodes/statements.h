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

using Statement = Instruction;  // backward compatibility

}  // namespace polyglot::ir
