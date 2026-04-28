/**
 * @file     wat_printer.cpp
 * @brief    WebAssembly text format (WAT) emitter
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <cstddef>
#include <iosfwd>
#include <ostream>
#include <sstream>
#include <string>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"

#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {

namespace {

void EmitInstructionWATImpl(std::ostream &os, const std::shared_ptr<ir::Instruction> &inst) {
  if (auto *bin = dynamic_cast<ir::BinaryInstruction *>(inst.get())) {
    using Op = ir::BinaryInstruction::Op;
    bool is_i64 = (bin->type.kind == ir::IRTypeKind::kI64);
    bool is_float = bin->type.IsFloat();
    const char *prefix = is_float ? "f64" : (is_i64 ? "i64" : "i32");
    switch (bin->op) {
    case Op::kAdd:
    case Op::kFAdd:
      os << "    " << prefix << ".add\n";
      break;
    case Op::kSub:
    case Op::kFSub:
      os << "    " << prefix << ".sub\n";
      break;
    case Op::kMul:
    case Op::kFMul:
      os << "    " << prefix << ".mul\n";
      break;
    case Op::kDiv:
    case Op::kSDiv:
      os << "    " << prefix << ".div_s\n";
      break;
    case Op::kUDiv:
      os << "    " << prefix << ".div_u\n";
      break;
    case Op::kFDiv:
      os << "    f64.div\n";
      break;
    case Op::kRem:
    case Op::kSRem:
      os << "    " << prefix << ".rem_s\n";
      break;
    case Op::kURem:
      os << "    " << prefix << ".rem_u\n";
      break;
    case Op::kFRem:
      os << "    f64.div  ;; no direct frem\n";
      break;
    case Op::kAnd:
      os << "    " << prefix << ".and\n";
      break;
    case Op::kOr:
      os << "    " << prefix << ".or\n";
      break;
    case Op::kXor:
      os << "    " << prefix << ".xor\n";
      break;
    case Op::kShl:
      os << "    " << prefix << ".shl\n";
      break;
    case Op::kLShr:
      os << "    " << prefix << ".shr_u\n";
      break;
    case Op::kAShr:
      os << "    " << prefix << ".shr_s\n";
      break;
    case Op::kCmpEq:
    case Op::kCmpFoe:
      os << "    " << prefix << ".eq\n";
      break;
    case Op::kCmpNe:
    case Op::kCmpFne:
      os << "    " << prefix << ".ne\n";
      break;
    case Op::kCmpLt:
    case Op::kCmpSlt:
    case Op::kCmpFlt:
      os << "    " << prefix << ".lt_s\n";
      break;
    case Op::kCmpSle:
    case Op::kCmpFle:
      os << "    " << prefix << ".le_s\n";
      break;
    case Op::kCmpSgt:
    case Op::kCmpFgt:
      os << "    " << prefix << ".gt_s\n";
      break;
    case Op::kCmpSge:
    case Op::kCmpFge:
      os << "    " << prefix << ".ge_s\n";
      break;
    case Op::kCmpUlt:
      os << "    " << prefix << ".lt_u\n";
      break;
    case Op::kCmpUle:
      os << "    " << prefix << ".le_u\n";
      break;
    case Op::kCmpUgt:
      os << "    " << prefix << ".gt_u\n";
      break;
    case Op::kCmpUge:
      os << "    " << prefix << ".ge_u\n";
      break;
    }
    return;
  }

  if (dynamic_cast<ir::ReturnStatement *>(inst.get())) {
    os << "    return\n";
    return;
  }

  if (auto *call = dynamic_cast<ir::CallInstruction *>(inst.get())) {
    os << "    call $" << call->callee << "\n";
    return;
  }

  if (dynamic_cast<ir::UnreachableStatement *>(inst.get())) {
    os << "    unreachable\n";
    return;
  }

  if (dynamic_cast<ir::BranchStatement *>(inst.get())) {
    os << "    br 0\n";
    return;
  }

  if (dynamic_cast<ir::CondBranchStatement *>(inst.get())) {
    os << "    br_if 0\n";
    return;
  }

  os << "    nop\n";
}

}  // namespace

std::string WasmTarget::EmitAssembly() {
  if (!module_)
    return "(module)\n";

  std::ostringstream os;
  os << "(module\n";

  // Emit memory
  os << "  (memory (export \"memory\") 1)\n";

  // Emit functions in WAT text format
  for (auto &fn : module_->Functions()) {
    os << "  (func $" << fn->name;

    // Export
    os << " (export \"" << fn->name << "\")";

    // Parameters
    for (size_t i = 0; i < fn->param_types.size(); ++i) {
      std::string pname = i < fn->params.size() ? fn->params[i] : ("p" + std::to_string(i));
      os << " (param $" << pname << " ";
      switch (IRTypeToWasm(fn->param_types[i])) {
      case WasmValType::kI32:
        os << "i32";
        break;
      case WasmValType::kI64:
        os << "i64";
        break;
      case WasmValType::kF32:
        os << "f32";
        break;
      case WasmValType::kF64:
        os << "f64";
        break;
      default:
        os << "i64";
        break;
      }
      os << ")";
    }

    // Return type
    if (fn->ret_type.kind != ir::IRTypeKind::kVoid) {
      os << " (result ";
      switch (IRTypeToWasm(fn->ret_type)) {
      case WasmValType::kI32:
        os << "i32";
        break;
      case WasmValType::kI64:
        os << "i64";
        break;
      case WasmValType::kF32:
        os << "f32";
        break;
      case WasmValType::kF64:
        os << "f64";
        break;
      default:
        os << "i64";
        break;
      }
      os << ")";
    }

    os << "\n";

    // Emit instruction comments for each block
    for (auto &blk : fn->blocks) {
      os << "    ;; block " << blk->name << "\n";
      for (auto &inst : blk->instructions) {
        EmitInstructionWAT(os, inst);
      }
      if (blk->terminator) {
        EmitInstructionWAT(os, blk->terminator);
      }
    }

    os << "  )\n";
  }

  os << ")\n";
  return os.str();
}

void WasmTarget::EmitInstructionWAT(std::ostream &os,
                                    const std::shared_ptr<ir::Instruction> &inst) {
  EmitInstructionWATImpl(os, inst);
}

}  // namespace polyglot::backends::wasm
