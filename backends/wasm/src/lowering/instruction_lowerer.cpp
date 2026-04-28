/**
 * @file     instruction_lowerer.cpp
 * @brief    IR instruction → WASM bytecode lowering
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <cstdint>
#include <string>
#include <vector>

#include "middle/include/ir/nodes/statements.h"

#include "backends/wasm/include/internal/wasm_constants.h"
#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {

using namespace internal;

void WasmTarget::LowerInstruction(const std::shared_ptr<ir::Instruction> &inst,
                                  std::vector<std::uint8_t> &body) {
  // Binary instructions
  if (auto *bin = dynamic_cast<ir::BinaryInstruction *>(inst.get())) {
    using Op = ir::BinaryInstruction::Op;
    bool is_i64 = (bin->type.kind == ir::IRTypeKind::kI64);
    bool is_float = bin->type.IsFloat();
    switch (bin->op) {
    case Op::kAdd:
      body.push_back(is_i64 ? kOpI64Add : kOpI32Add);
      break;
    case Op::kSub:
      body.push_back(is_i64 ? kOpI64Sub : kOpI32Sub);
      break;
    case Op::kMul:
      body.push_back(is_i64 ? kOpI64Mul : kOpI32Mul);
      break;
    case Op::kDiv:
    case Op::kSDiv:
      body.push_back(is_i64 ? kOpI64DivS : kOpI32DivS);
      break;
    case Op::kUDiv:
      body.push_back(is_i64 ? kOpI64DivU : kOpI32DivU);
      break;
    case Op::kRem:
    case Op::kSRem:
      body.push_back(is_i64 ? kOpI64RemS : kOpI32RemS);
      break;
    case Op::kURem:
      body.push_back(is_i64 ? kOpI64RemU : kOpI32RemU);
      break;
    case Op::kAnd:
      body.push_back(is_i64 ? kOpI64And : kOpI32And);
      break;
    case Op::kOr:
      body.push_back(is_i64 ? kOpI64Or : kOpI32Or);
      break;
    case Op::kXor:
      body.push_back(is_i64 ? kOpI64Xor : kOpI32Xor);
      break;
    case Op::kShl:
      body.push_back(is_i64 ? kOpI64Shl : kOpI32Shl);
      break;
    case Op::kLShr:
      body.push_back(is_i64 ? kOpI64ShrU : kOpI32ShrU);
      break;
    case Op::kAShr:
      body.push_back(is_i64 ? kOpI64ShrS : kOpI32ShrS);
      break;
    case Op::kFAdd:
      body.push_back(kOpF64Add);
      break;
    case Op::kFSub:
      body.push_back(kOpF64Sub);
      break;
    case Op::kFMul:
      body.push_back(kOpF64Mul);
      break;
    case Op::kFDiv:
      body.push_back(kOpF64Div);
      break;
    case Op::kFRem: {
      // WASM has no frem instruction.  Lower as: x - trunc(x/y) * y
      // Stack before: [x, y]
      // We need to duplicate x and y.  Since WASM doesn't have dup,
      // we use local.tee + local.get if available, but for a simpler
      // approach we rely on the operands already being on the stack
      // twice (the IR builder should have arranged this).
      // Emit: x - trunc(x / y) * y
      // The operands are already duplicated by the emitter before
      // reaching this point, so we emit the arithmetic directly.
      // [x, y, x, y] -> div -> trunc -> [x, y, trunc(x/y)]
      // -> mul -> [x, trunc(x/y)*y] -> sub -> [x - trunc(x/y)*y]
      body.push_back(kOpF64Div);
      body.push_back(kOpF64Trunc);
      body.push_back(kOpF64Mul);
      body.push_back(kOpF64Sub);
      break;
    }
    case Op::kCmpEq:
      body.push_back(is_float ? kOpF64Eq : (is_i64 ? kOpI64Eq : kOpI32Eq));
      break;
    case Op::kCmpNe:
      body.push_back(is_float ? kOpF64Ne : (is_i64 ? kOpI64Ne : kOpI32Ne));
      break;
    case Op::kCmpLt:
    case Op::kCmpSlt:
      body.push_back(is_float ? kOpF64Lt : (is_i64 ? kOpI64LtS : kOpI32LtS));
      break;
    case Op::kCmpSle:
      body.push_back(is_float ? kOpF64Le : (is_i64 ? kOpI64LeS : kOpI32LeS));
      break;
    case Op::kCmpSgt:
      body.push_back(is_float ? kOpF64Gt : (is_i64 ? kOpI64GtS : kOpI32GtS));
      break;
    case Op::kCmpSge:
      body.push_back(is_float ? kOpF64Ge : (is_i64 ? kOpI64GeS : kOpI32GeS));
      break;
    case Op::kCmpUlt:
      body.push_back(is_i64 ? kOpI64LtU : kOpI32LtU);
      break;
    case Op::kCmpUle:
      body.push_back(is_i64 ? kOpI64LeU : kOpI32LeU);
      break;
    case Op::kCmpUgt:
      body.push_back(is_i64 ? kOpI64GtU : kOpI32GtU);
      break;
    case Op::kCmpUge:
      body.push_back(is_i64 ? kOpI64GeU : kOpI32GeU);
      break;
    case Op::kCmpFoe:
      body.push_back(kOpF64Eq);
      break;
    case Op::kCmpFne:
      body.push_back(kOpF64Ne);
      break;
    case Op::kCmpFlt:
      body.push_back(kOpF64Lt);
      break;
    case Op::kCmpFle:
      body.push_back(kOpF64Le);
      break;
    case Op::kCmpFgt:
      body.push_back(kOpF64Gt);
      break;
    case Op::kCmpFge:
      body.push_back(kOpF64Ge);
      break;
    }
    return;
  }

  // Return
  if (dynamic_cast<ir::ReturnStatement *>(inst.get())) {
    body.push_back(kOpReturn);
    return;
  }

  // Call
  if (auto *call = dynamic_cast<ir::CallInstruction *>(inst.get())) {
    // Resolve the callee name to its WASM function index.
    auto it = func_name_to_index_.find(call->callee);
    if (it != func_name_to_index_.end()) {
      body.push_back(kOpCall);
      EmitU32Leb128(body, it->second);
    } else {
      // Unresolved callee is a hard error — the resulting module would
      // trap or call the wrong function.  Record the error and emit
      // unreachable to guarantee a deterministic failure.
      lowering_errors_.push_back("unresolved call target '" + call->callee +
                                 "'; cannot emit valid call instruction");
      body.push_back(kOpUnreachable);
    }
    return;
  }

  // Load
  if (auto *load = dynamic_cast<ir::LoadInstruction *>(inst.get())) {
    bool is_i64 = (load->type.kind == ir::IRTypeKind::kI64);
    bool is_f32 = (load->type.kind == ir::IRTypeKind::kF32);
    bool is_f64 = (load->type.kind == ir::IRTypeKind::kF64);
    if (is_f64)
      body.push_back(kOpF64Load);
    else if (is_f32)
      body.push_back(kOpF32Load);
    else if (is_i64)
      body.push_back(kOpI64Load);
    else
      body.push_back(kOpI32Load);
    // alignment + offset (both LEB128)
    std::uint32_t align_log2 = is_i64 || is_f64 ? 3 : 2;
    EmitU32Leb128(body, align_log2);
    EmitU32Leb128(body, 0); // offset 0
    return;
  }

  // Store
  if (auto *store = dynamic_cast<ir::StoreInstruction *>(inst.get())) {
    // Determine value type from the second operand; default to i32
    bool is_i64 = false;
    bool is_f32 = false;
    bool is_f64 = false;
    if (!store->operands.empty()) {
      // The value operand type determines the store width
      auto vt = store->type;
      is_i64 = (vt.kind == ir::IRTypeKind::kI64);
      is_f32 = (vt.kind == ir::IRTypeKind::kF32);
      is_f64 = (vt.kind == ir::IRTypeKind::kF64);
    }
    if (is_f64)
      body.push_back(kOpF64Store);
    else if (is_f32)
      body.push_back(kOpF32Store);
    else if (is_i64)
      body.push_back(kOpI64Store);
    else
      body.push_back(kOpI32Store);
    std::uint32_t align_log2 = is_i64 || is_f64 ? 3 : 2;
    EmitU32Leb128(body, align_log2);
    EmitU32Leb128(body, 0);
    return;
  }

  // Cast
  if (auto *cast = dynamic_cast<ir::CastInstruction *>(inst.get())) {
    using CK = ir::CastInstruction::CastKind;
    switch (cast->cast) {
    case CK::kZExt:
    case CK::kIntToPtr:
      body.push_back(kOpI64ExtendI32U);
      break;
    case CK::kSExt:
      body.push_back(kOpI64ExtendI32S);
      break;
    case CK::kTrunc:
    case CK::kPtrToInt:
      body.push_back(kOpI32WrapI64);
      break;
    case CK::kBitcast:
    case CK::kFpExt:
    case CK::kFpTrunc:
      body.push_back(kOpNop); // no-op for same-size reinterpret
      break;
    }
    return;
  }

  // Alloca — modeled via the WASM shadow stack.
  // The shadow stack pointer lives in global 0 (__stack_pointer).  An alloca
  // of `size` bytes is lowered as:
  //   global.get $__stack_pointer
  //   i32.const  <size>
  //   i32.sub
  //   local.tee  <result>       ;; the allocated address
  //   global.set $__stack_pointer
  // The caller is responsible for restoring the stack pointer on function
  // exit (see LowerFunction's epilogue).
  if (auto *alloca_inst = dynamic_cast<ir::AllocaInstruction *>(inst.get())) {
    // Determine allocation size from the pointed-to type
    std::uint32_t alloc_size = 16; // default conservative alignment

    auto pointee = alloca_inst->type;
    if (pointee.kind == ir::IRTypeKind::kPointer && !pointee.subtypes.empty()) {
      auto &inner = pointee.subtypes[0];
      if (inner.kind == ir::IRTypeKind::kI32 || inner.kind == ir::IRTypeKind::kF32)
        alloc_size = 4;
      else if (inner.kind == ir::IRTypeKind::kI64 || inner.kind == ir::IRTypeKind::kF64)
        alloc_size = 8;
      else if (inner.kind == ir::IRTypeKind::kI8)
        alloc_size = 1;
      else if (inner.kind == ir::IRTypeKind::kI16)
        alloc_size = 2;
    }

    body.push_back(kOpGlobalGet);
    EmitU32Leb128(body, shadow_stack_global_); // __stack_pointer
    body.push_back(kOpI32Const);
    EmitI32Leb128(body, static_cast<std::int32_t>(alloc_size));
    body.push_back(kOpI32Sub);
    // The result (new stack pointer value) is left on the operand stack
    // for subsequent use as the pointer.  Also write it back.
    body.push_back(kOpGlobalSet);
    EmitU32Leb128(body, shadow_stack_global_);
    // Push the new pointer again so the instruction has a result.
    body.push_back(kOpGlobalGet);
    EmitU32Leb128(body, shadow_stack_global_);
    return;
  }

  // Unreachable
  if (dynamic_cast<ir::UnreachableStatement *>(inst.get())) {
    body.push_back(kOpUnreachable);
    return;
  }

  // Branch (unconditional) → br
  if (auto *br = dynamic_cast<ir::BranchStatement *>(inst.get())) {
    body.push_back(kOpBr);
    // Compute relative block depth from current position to target
    std::uint32_t depth = 0;
    if (br->target && !br->target->name.empty()) {
      auto it = block_depth_map_.find(br->target->name);
      if (it != block_depth_map_.end()) {
        // depth = current nesting - target block index
        depth = current_block_depth_ > it->second ? current_block_depth_ - it->second - 1 : 0;
      } else {
        lowering_errors_.push_back("branch target '" + br->target->name +
                                   "' not in block depth map");
      }
    }
    EmitU32Leb128(body, depth);
    return;
  }

  // Conditional branch → br_if
  if (auto *cbr = dynamic_cast<ir::CondBranchStatement *>(inst.get())) {
    body.push_back(kOpBrIf);
    std::uint32_t depth = 0;
    if (cbr->true_target && !cbr->true_target->name.empty()) {
      auto it = block_depth_map_.find(cbr->true_target->name);
      if (it != block_depth_map_.end()) {
        depth = current_block_depth_ > it->second ? current_block_depth_ - it->second - 1 : 0;
      } else {
        lowering_errors_.push_back("br_if target '" + cbr->true_target->name +
                                   "' not in block depth map");
      }
    }
    EmitU32Leb128(body, depth);
    return;
  }

  // Unsupported instruction — emit unreachable trap and record diagnostic.
  // Previously this was a silent nop, which would produce incorrect code.
  std::string inst_name = inst->name.empty() ? "(anonymous)" : inst->name;
  lowering_errors_.push_back("unsupported IR instruction '" + inst_name +
                             "' (kind: " + std::to_string(static_cast<int>(inst->type.kind)) +
                             "); emitting unreachable");
  body.push_back(kOpUnreachable);
}

}  // namespace polyglot::backends::wasm
