#include "common/include/ir/ir_printer.h"

#include <sstream>

namespace polyglot::ir {

namespace {
std::string TypeToString(const IRType &t) {
  switch (t.kind) {
    case IRTypeKind::kPointer:
      return TypeToString(t.subtypes.empty() ? IRType::Invalid() : t.subtypes[0]) + "*";
    case IRTypeKind::kReference:
      return TypeToString(t.subtypes.empty() ? IRType::Invalid() : t.subtypes[0]) + "&";
    case IRTypeKind::kArray:
      return "[" + std::to_string(t.count) + " x " + TypeToString(t.subtypes.empty() ? IRType::Invalid() : t.subtypes[0]) + "]";
    case IRTypeKind::kVector:
      return "<" + std::to_string(t.count) + " x " + TypeToString(t.subtypes.empty() ? IRType::Invalid() : t.subtypes[0]) + ">";
    case IRTypeKind::kStruct: {
      std::string s = "%" + t.name + " {";
      for (size_t i = 0; i < t.subtypes.size(); ++i) {
        if (i) s += ", ";
        s += TypeToString(t.subtypes[i]);
      }
      s += "}";
      return s;
    }
    case IRTypeKind::kFunction: {
      if (t.subtypes.empty()) return "fn";
      std::string s = TypeToString(t.subtypes[0]) + " (";
      for (size_t i = 1; i < t.subtypes.size(); ++i) {
        if (i > 1) s += ", ";
        s += TypeToString(t.subtypes[i]);
      }
      s += ")";
      return s;
    }
    case IRTypeKind::kInvalid:
    case IRTypeKind::kI1:
    case IRTypeKind::kI8:
    case IRTypeKind::kI32:
    case IRTypeKind::kI64:
    case IRTypeKind::kF32:
    case IRTypeKind::kF64:
    case IRTypeKind::kVoid:
      return t.name;
  }
  return t.name;
}

void PrintInst(const Instruction &inst, std::ostream &os) {
  auto print_ops = [&](const std::vector<std::string> &ops) {
    for (size_t i = 0; i < ops.size(); ++i) {
      if (i) os << ", ";
      os << ops[i];
    }
  };
  if (inst.HasResult()) {
    os << inst.name << " = ";
  }
  if (auto bin = dynamic_cast<const BinaryInstruction *>(&inst)) {
    os << "bin(" << static_cast<int>(bin->op) << ": ";
    print_ops(bin->operands);
    os << ")";
  } else if (auto phi = dynamic_cast<const PhiInstruction *>(&inst)) {
    os << "phi ";
    for (size_t i = 0; i < phi->incomings.size(); ++i) {
      if (i) os << ", ";
      os << "[" << (phi->incomings[i].first ? phi->incomings[i].first->name : "?") << ": "
         << phi->incomings[i].second << "]";
    }
  } else if (auto call = dynamic_cast<const CallInstruction *>(&inst)) {
    os << "call " << (call->is_indirect ? "*" : "") << call->callee << "(";
    print_ops(call->operands);
    os << ")";
  } else if (dynamic_cast<const AllocaInstruction *>(&inst)) {
    os << "alloca";
  } else if (auto ld = dynamic_cast<const LoadInstruction *>(&inst)) {
    os << "load ";
    print_ops(ld->operands);
  } else if (auto st = dynamic_cast<const StoreInstruction *>(&inst)) {
    os << "store ";
    print_ops(st->operands);
  } else if (auto cast = dynamic_cast<const CastInstruction *>(&inst)) {
    os << "cast " << static_cast<int>(cast->cast) << " ";
    print_ops(cast->operands);
  } else if (auto gep = dynamic_cast<const GetElementPtrInstruction *>(&inst)) {
    os << "gep " << (gep->operands.empty() ? "" : gep->operands[0]) << " [";
    for (size_t i = 0; i < gep->indices.size(); ++i) {
      if (i) os << ", ";
      os << gep->indices[i];
    }
    os << "]";
  } else if (dynamic_cast<const ReturnStatement *>(&inst)) {
    os << "ret";
    if (!inst.operands.empty()) {
      os << " " << inst.operands[0];
    }
  } else if (auto br = dynamic_cast<const BranchStatement *>(&inst)) {
    os << "br -> " << (br->target ? br->target->name : "<null>");
  } else if (auto cbr = dynamic_cast<const CondBranchStatement *>(&inst)) {
    os << "cbr " << (cbr->operands.empty() ? "" : cbr->operands[0]) << " ? "
       << (cbr->true_target ? cbr->true_target->name : "<null>") << " : "
       << (cbr->false_target ? cbr->false_target->name : "<null>");
  } else if (auto sw = dynamic_cast<const SwitchStatement *>(&inst)) {
    os << "switch " << (sw->operands.empty() ? "" : sw->operands[0]) << " {";
    for (auto &c : sw->cases) {
      os << " " << c.value << " -> " << (c.target ? c.target->name : "<null>");
    }
    os << " default -> " << (sw->default_target ? sw->default_target->name : "<null>") << " }";
  } else {
    os << "inst";
  }
  os << " : " << TypeToString(inst.type);
}
}  // namespace

void PrintFunction(const Function &func, std::ostream &os) {
  os << "func " << func.name << "(";
  for (size_t i = 0; i < func.params.size(); ++i) {
    if (i) os << ", ";
    os << func.params[i];
  }
  os << ")\n";
  for (auto &bb_ptr : func.blocks) {
    auto *bb = bb_ptr.get();
    os << bb->name << ":\n";
    for (auto &phi : bb->phis) {
      os << "  ";
      PrintInst(*phi, os);
      os << "\n";
    }
    for (auto &inst : bb->instructions) {
      os << "  ";
      PrintInst(*inst, os);
      os << "\n";
    }
    if (bb->terminator) {
      os << "  ";
      PrintInst(*bb->terminator, os);
      os << "\n";
    }
  }
}

void PrintModule(const IRContext &ctx, std::ostream &os) {
  for (auto &g : ctx.Globals()) {
    os << (g->is_const ? "const " : "global ") << g->name << " : " << TypeToString(g->type);
    if (auto cs = std::dynamic_pointer_cast<ConstantString>(g->initializer)) {
      os << " = \"" << cs->data << (cs->null_terminated ? "\\0" : "") << "\"";
    } else if (auto gep = std::dynamic_pointer_cast<ConstantGEP>(g->initializer)) {
      os << " = gep @" << (gep->base ? gep->base->name : "<null>") << " [";
      for (size_t i = 0; i < gep->indices.size(); ++i) {
        if (i) os << ", ";
        os << gep->indices[i];
      }
      os << "]";
    } else if (!g->init.empty()) {
      os << " = " << g->init;
    }
    os << "\n";
  }
  for (auto &fn : ctx.Functions()) {
    PrintFunction(*fn, os);
  }
}

std::string Dump(const Function &func) {
  std::ostringstream oss;
  PrintFunction(func, oss);
  return oss.str();
}

}  // namespace polyglot::ir
