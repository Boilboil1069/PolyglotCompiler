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
  bool is_dead{false};  // Marked for removal by dead code elimination

  virtual bool IsTerminator() const { return false; }
  bool HasResult() const { return !name.empty() && type.kind != IRTypeKind::kVoid; }
};

struct BinaryInstruction : Instruction {
  enum class Op {
    kAdd,
    kSub,
    kMul,
    kDiv,   // legacy alias: treated as signed div
    kSDiv,
    kUDiv,
    kSRem,
    kURem,
    kRem,   // legacy alias: treated as signed rem
    kFAdd,  // Floating-point add
    kFSub,  // Floating-point subtract
    kFMul,  // Floating-point multiply
    kFDiv,  // Floating-point divide
    kFRem,  // Floating-point remainder
    kAnd,
    kOr,
    kXor,
    kShl,
    kLShr,
    kAShr,
    kCmpEq,
    kCmpNe,
    kCmpUlt,
    kCmpUle,
    kCmpUgt,
    kCmpUge,
    kCmpSlt,
    kCmpSle,
    kCmpSgt,
    kCmpSge,
    kCmpFoe,  // Floating-point ordered equal
    kCmpFne,  // Floating-point ordered not equal
    kCmpFlt,  // Floating-point ordered less than
    kCmpFle,  // Floating-point ordered less than or equal
    kCmpFgt,  // Floating-point ordered greater than
    kCmpFge,  // Floating-point ordered greater than or equal
    kCmpLt
  };
  Op op {Op::kAdd};
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
  IRType callee_type{IRType::Invalid()};  // optional function type annotation
  bool is_vararg{false};
  bool is_tail_call{false};  // Tail call optimization flag
};

struct AllocaInstruction : Instruction {
  bool no_escape{false};  // Set by escape analysis if allocation doesn't escape
  AllocaInstruction() { type = IRType::Pointer(IRType::Invalid()); }
};

struct LoadInstruction : Instruction {
  size_t align{0};  // 0 means natural alignment
  LoadInstruction() { type = IRType::Invalid(); }
};

struct StoreInstruction : Instruction {
  size_t align{0};  // 0 means natural alignment
  StoreInstruction() { type = IRType::Void(); }
};

struct CastInstruction : Instruction {
  enum class CastKind { kZExt, kSExt, kTrunc, kBitcast, kFpExt, kFpTrunc, kIntToPtr, kPtrToInt };
  CastKind cast{CastKind::kBitcast};
};

struct GetElementPtrInstruction : Instruction {
  IRType source_type{IRType::Invalid()};
  std::vector<size_t> indices;
  bool inbounds{false};
  GetElementPtrInstruction() { type = IRType::Pointer(IRType::Invalid()); }
};

struct MemcpyInstruction : Instruction {
  size_t align{0};
  MemcpyInstruction() { type = IRType::Void(); }
};

struct MemsetInstruction : Instruction {
  size_t align{0};
  MemsetInstruction() { type = IRType::Void(); }
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

struct UnreachableStatement : Instruction {
  UnreachableStatement() { type = IRType::Void(); }
  bool IsTerminator() const override { return true; }
};

// Exception handling instructions
struct InvokeInstruction : Instruction {
  std::string callee;
  BasicBlock *normal_dest{nullptr};  // Success continuation
  BasicBlock *unwind_dest{nullptr};  // Exception landing pad
  bool is_indirect{false};
  IRType callee_type{IRType::Invalid()};
};

struct LandingPadInstruction : Instruction {
  bool is_cleanup{false};  // Is this a cleanup landing pad?
  std::vector<IRType> catch_types;  // Types this pad catches
  LandingPadInstruction() { type = IRType::Invalid(); }
};

struct ResumeInstruction : Instruction {
  ResumeInstruction() { type = IRType::Void(); }
  bool IsTerminator() const override { return true; }
};

// SIMD/Vector instructions
struct VectorInstruction : Instruction {
  enum class VecOp {
    // Arithmetic
    kVecAdd,
    kVecSub,
    kVecMul,
    kVecDiv,
    kVecFAdd,
    kVecFSub,
    kVecFMul,
    kVecFDiv,
    // Logical
    kVecAnd,
    kVecOr,
    kVecXor,
    // Comparison
    kVecCmpEq,
    kVecCmpNe,
    kVecCmpLt,
    kVecCmpLe,
    kVecCmpGt,
    kVecCmpGe,
    // Special operations
    kVecShuffle,
    kVecBroadcast,
    kVecExtract,
    kVecInsert,
    kVecMin,
    kVecMax,
    kVecSqrt,
    kVecRcp,   // Reciprocal
    kVecRsqrt  // Reciprocal square root
  };
  VecOp op{VecOp::kVecAdd};
  std::vector<int> shuffle_mask;  // For shuffle operation
  int extract_index{0};            // For extract operation
  int insert_index{0};             // For insert operation
};

using Statement = Instruction;  // backward compatibility

}  // namespace polyglot::ir
