// Scalar AArch64 emitter with CFG-aware allocation into callee-saved registers.
// Spilled SSA values and phi parallel-copy scratch have stable frame slots.
#include "backends/arm64/include/arm64_target.h"
#include "native_registers.h"
#include "print_f64_encoding.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>

namespace polyglot::backends::arm64 {
namespace {
using namespace ir;
using Result = Arm64Target::MCResult;
class NativeEmitter {
  const IRContext &module;
  std::string target_os;
  RegAllocStrategy allocation_strategy;
  std::unordered_map<std::string, unsigned> registers;
  std::map<unsigned, std::size_t> saved_registers;
  Result result;
  std::vector<std::uint8_t> code;
  std::unordered_map<std::string, std::size_t> functions;
  struct Fixup { std::size_t offset; std::string name; };
  std::vector<Fixup> calls;
  std::unordered_map<std::string, std::size_t> slots, objects;
  std::unordered_map<std::string, IRType> types;
  std::unordered_map<const BasicBlock *, std::size_t> blocks;
  struct Jump { std::size_t offset; const BasicBlock *target; };
  std::vector<Jump> jumps;
  std::size_t extent{}, phi_scratch{}, frame{};
  const Function *function{};

  [[noreturn]] void Fail(const std::string &message) const {
    throw std::runtime_error("ARM64 native emission: " + message);
  }
  void Emit(std::uint32_t word) {
    for (int i = 0; i < 4; ++i) code.push_back((word >> (i * 8)) & 255);
  }
  void Patch(std::size_t at, std::uint32_t word) {
    for (int i = 0; i < 4; ++i) code.at(at + i) = (word >> (i * 8)) & 255;
  }
  void BranchAt(std::size_t at, std::size_t to, bool call = false) {
    auto distance = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(at);
    if (distance % 4 || distance < -(1LL << 27) || distance >= (1LL << 27))
      Fail("branch exceeds signed 26-bit range");
    Patch(at, (call ? 0x94000000u : 0x14000000u) |
                  (static_cast<std::uint32_t>(distance / 4) & 0x03ffffffu));
  }
  void Imm(unsigned reg, std::uint64_t value) {
    Emit(0xD2800000u | ((value & 65535) << 5) | reg);
    for (unsigned i = 1; i < 4; ++i)
      if ((value >> (i * 16)) & 65535)
        Emit(0xF2800000u | (i << 21) | (((value >> (i * 16)) & 65535) << 5) | reg);
  }
  void Address(unsigned reg, std::size_t offset) {
    if (offset <= 4095) Emit(0x910003E0u | (offset << 10) | reg); // add xN, sp, #offset
    else { Imm(reg, offset); Emit(0x8B2063E0u | (reg << 16) | reg); } // add xN, sp, xN
  }
  void Slot(unsigned reg, std::size_t offset, bool store) {
    if (offset % 8 == 0 && offset / 8 < 4096)
      Emit((store ? 0xF90003E0u : 0xF94003E0u) | ((offset / 8) << 10) | reg);
    else { Address(17, offset); Emit((store ? 0xF9000000u : 0xF9400000u) | (17 << 5) | reg); }
  }
  static bool Integer(const std::string &name, std::uint64_t &value) {
    char *end = nullptr;
    value = std::strtoull(name.c_str(), &end, 0);
    return end != name.c_str() && *end == '\0';
  }
  IRType Type(const std::string &name) const {
    auto it = types.find(name);
    if (it != types.end()) return it->second;
    for (const auto &global : module.Globals())
      if (global->name == name) return IRType::Pointer(global->type);
    return IRType::I64();
  }
  void Load(const std::string &name, unsigned reg) {
    std::uint64_t value;
    if (Integer(name, value)) { Imm(reg, value); return; }
    auto allocated = registers.find(name);
    if (allocated != registers.end()) { Move(reg, allocated->second); return; }
    auto found = slots.find(name);
    if (found != slots.end()) { Slot(reg, found->second, false); return; }
    for (const auto &g : module.Globals()) if (g->name == name) {
      auto offset = code.size();
      Emit(0x90000000u | reg);
      Emit(0x91000000u | (reg << 5) | reg);
      result.relocs.push_back({".text", static_cast<std::uint32_t>(offset), 2, name, 0});
      result.relocs.push_back({".text", static_cast<std::uint32_t>(offset + 4), 3, name, 0});
      return;
    }
    Fail("undefined value '" + name + "' in " + function->name);
  }
  void Move(unsigned destination, unsigned source) {
    if (destination != source) Emit(0xAA0003E0u | (source << 16) | destination);
  }
  void StoreValue(const std::string &name, unsigned reg) {
    auto allocated = registers.find(name);
    if (allocated != registers.end()) Move(allocated->second, reg);
    else Slot(reg, slots.at(name), true);
  }
  void Normalize(unsigned reg, const IRType &type) {
    unsigned bits = type.kind == IRTypeKind::kI1 ? 1 :
                    type.kind == IRTypeKind::kI8 ? 8 :
                    type.kind == IRTypeKind::kI16 ? 16 :
                    type.kind == IRTypeKind::kI32 ? 32 : 64;
    if (bits < 64)
      Emit((type.IsSigned() ? 0x93400000u : 0xD3400000u) | ((bits - 1) << 10) | (reg << 5) | reg);
  }
  void Save(const Instruction &inst, unsigned reg = 9) {
    if (!inst.HasResult()) return;
    Normalize(reg, inst.type);
    StoreValue(inst.name, reg);
  }
  std::size_t Allocate(std::size_t size = 8, std::size_t align = 8) {
    extent = (extent + align - 1) / align * align;
    auto offset = extent; extent += size; return offset;
  }
  void Define(const std::string &name, const IRType &type) {
    if (name.empty()) return;
    if (!registers.count(name) && !slots.count(name)) slots[name] = Allocate();
    types[name] = type;
  }
  void Edge(const BasicBlock *from, const BasicBlock *to) {
    if (!to) Fail("null branch destination");
    // Two passes implement a parallel copy, including cycles on back edges.
    std::size_t i = 0;
    for (const auto &phi : to->phis) {
      auto incoming = std::find_if(phi->incomings.begin(), phi->incomings.end(),
                                  [&](const auto &v) { return v.first == from; });
      if (incoming == phi->incomings.end()) Fail("missing phi predecessor");
      if (incoming->second == "undef") Imm(9, 0); // unobservable SSA undef on an unused path
      else Load(incoming->second, 9);
      Slot(9, phi_scratch + i++ * 8, true);
    }
    i = 0;
    for (const auto &phi : to->phis) {
      Slot(9, phi_scratch + i++ * 8, false);
      StoreValue(phi->name, 9);
    }
    jumps.push_back({code.size(), to}); Emit(0x14000000u);
  }
  void Epilogue() {
    for (const auto &[reg, offset] : saved_registers) Slot(reg, offset, false);
    Emit(0x910003BFu); // mov sp, x29
    Emit(0xA8C17BFDu); // ldp x29, x30, [sp], #16
    Emit(0xD65F03C0u);
  }
  void Binary(const BinaryInstruction &bin) {
    using Op = BinaryInstruction::Op;
    Load(bin.operands.at(0), 9); Load(bin.operands.at(1), 10);
    const bool fp = Type(bin.operands[0]).IsFloat() || Type(bin.operands[1]).IsFloat() ||
                    bin.type.IsFloat();
    if (fp) {
      const bool single = Type(bin.operands[0]).kind == IRTypeKind::kF32 ||
                          Type(bin.operands[1]).kind == IRTypeKind::kF32 ||
                          bin.type.kind == IRTypeKind::kF32;
      const std::uint32_t precision = single ? 0 : 0x00400000u;
      Emit((single ? 0x1E270000u : 0x9E670000u) | (9 << 5)); // fmov d0,x9
      Emit((single ? 0x1E270000u : 0x9E670000u) | (10 << 5) | 1);
      std::uint32_t opcode = 0;
      switch (bin.op) {
      case Op::kAdd: case Op::kFAdd: opcode = 0x1E212800u; break;
      case Op::kSub: case Op::kFSub: opcode = 0x1E213800u; break;
      case Op::kMul: case Op::kFMul: opcode = 0x1E210800u; break;
      case Op::kDiv: case Op::kFDiv: opcode = 0x1E211800u; break;
      default: break;
      }
      if (opcode) {
        Emit(opcode | precision);
        Emit((single ? 0x1E260000u : 0x9E660000u) | 9);
        Save(bin); return;
      }
      Emit(0x1E212000u | precision); // fcmp d0,d1
    }
    std::uint32_t op = 0; int cond = -1;
    switch (bin.op) {
    case Op::kAdd: op = 0x8B0A0129u; break;
    case Op::kSub: op = 0xCB0A0129u; break;
    case Op::kMul: op = 0x9B0A7D29u; break;
    case Op::kDiv: case Op::kSDiv: op = 0x9ACA0D29u; break;
    case Op::kUDiv: op = 0x9ACA0929u; break;
    case Op::kRem: case Op::kSRem: case Op::kURem:
      Emit(bin.op == Op::kURem ? 0x9ACA092Bu : 0x9ACA0D2Bu); // x11 = x9 / x10
      op = 0x9B0AA569u; break; // msub x9,x11,x10,x9
    case Op::kAnd: op = 0x8A0A0129u; break;
    case Op::kOr: op = 0xAA0A0129u; break;
    case Op::kXor: op = 0xCA0A0129u; break;
    case Op::kShl: op = 0x9ACA2129u; break;
    case Op::kLShr: op = 0x9ACA2529u; break;
    case Op::kAShr: op = 0x9ACA2929u; break;
    case Op::kCmpEq: case Op::kCmpFoe: cond = 0; break;
    case Op::kCmpNe: cond = 1; break;
    case Op::kCmpFne:
      // Ordered != must be false for unordered operands (V flag set).
      Emit(0x9A9F07E9u); // cset x9,ne
      Emit(0x9A9F67EAu); // cset x10,vc
      Emit(0x8A0A0129u); Save(bin); return;
    case Op::kCmpUlt: cond = 3; break;
    case Op::kCmpUle: cond = 9; break;
    case Op::kCmpUgt: cond = 8; break;
    case Op::kCmpUge: cond = 2; break;
    case Op::kCmpLt: case Op::kCmpSlt: cond = 11; break;
    case Op::kCmpSle: cond = 13; break;
    case Op::kCmpSgt: cond = 12; break;
    case Op::kCmpSge: cond = 10; break;
    case Op::kCmpFlt: cond = 4; break;
    case Op::kCmpFle: cond = 9; break;
    case Op::kCmpFgt: cond = 12; break;
    case Op::kCmpFge: cond = 10; break;
    default: Fail("unsupported binary operation");
    }
    if (cond >= 0) {
      if (!fp) Emit(0xEB0A013Fu); // cmp x9,x10
      Emit(0x9A9F07E9u | ((cond ^ 1) << 12)); // cset x9,cond
    } else Emit(op);
    Save(bin);
  }
  void Println(const CallInstruction &call) {
    if (target_os != "darwin" && target_os != "macos" && target_os != "linux")
      Fail("PRINTLN currently requires Darwin or Linux");
    if (call.operands.size() != 2) Fail("PRINTLN expects one static string");
    std::shared_ptr<Value> value;
    for (const auto &g : module.Globals())
      if (g->name == call.operands[0]) value = g->initializer;
    if (auto gep = std::dynamic_pointer_cast<ConstantGEP>(value)) {
      if (auto base = std::dynamic_pointer_cast<GlobalValue>(gep->base)) value = base->initializer;
    }
    auto str = std::dynamic_pointer_cast<ConstantString>(value);
    if (!str) Fail("PRINTLN requires a statically known string");
    std::string bytes = str->data + "\n";
    if (bytes.size() > 65536) Fail("PRINTLN literal exceeds 64 KiB");
    Imm(0, 1);
    auto address = code.size(); Emit(0x10000001u); // adr x1,payload
    Imm(2, bytes.size());
    const bool darwin = target_os != "linux";
    Imm(darwin ? 16 : 8, darwin ? 4 : 64);
    Emit(darwin ? 0xD4001001u : 0xD4000001u);
    auto skip = code.size(); Emit(0x14000000u);
    const auto payload = code.size();
    code.insert(code.end(), bytes.begin(), bytes.end());
    while (code.size() % 4) code.push_back(0);
    auto delta = static_cast<std::uint32_t>(payload - address);
    Patch(address, 0x10000001u | ((delta & 3) << 29) | ((delta >> 2) << 5));
    BranchAt(skip, code.size());
  }
  void PrintInteger(const CallInstruction &call) {
    if (target_os != "darwin" && target_os != "macos" && target_os != "linux")
      Fail("print_i64 currently requires Darwin or Linux");
    if (call.operands.size() != 1 || !Type(call.operands[0]).IsInteger())
      Fail("print_i64 expects one integer");
    Load(call.operands[0], 9);
    Emit(0xD10083FFu); // sub sp,sp,#32: enough for sign, 20 digits and newline
    Emit(0x937FFD2Du); // asr x13,x9,#63
    Emit(0xEB1F013Fu); // cmp x9,#0
    Emit(0xDA895529u); // cneg x9,x9,mi; INT64_MIN becomes unsigned 2^63
    Emit(0x91007FE1u); // add x1,sp,#31
    Imm(10, 10);
    Emit(0x3900002Au); // strb w10,[x1]
    const auto loop = code.size();
    Emit(0x9ACA092Bu); // udiv x11,x9,x10
    Emit(0x9B0AA56Cu); // msub x12,x11,x10,x9
    Emit(0x1100C18Cu); // add w12,w12,#48
    Emit(0xD1000421u); // sub x1,x1,#1
    Emit(0x3900002Cu); // strb w12,[x1]
    Move(9, 11);
    const auto again = code.size();
    Emit(0xB5000009u | ((static_cast<std::uint32_t>((static_cast<std::int64_t>(loop) - again) / 4) & 0x7ffff) << 5));
    const auto nonnegative = code.size(); Emit(0xB400000Du); // cbz x13,write
    Emit(0xD1000421u); Imm(12, '-'); Emit(0x3900002Cu);
    Patch(nonnegative, 0xB400000Du | (((code.size() - nonnegative) / 4) << 5));
    Emit(0x910083E2u); // add x2,sp,#32
    Emit(0xCB010042u); // sub x2,x2,x1
    Imm(0, 1);
    const bool darwin = target_os != "linux";
    Imm(darwin ? 16 : 8, darwin ? 4 : 64);
    Emit(darwin ? 0xD4001001u : 0xD4000001u);
    Emit(0x910083FFu); // add sp,sp,#32
  }
  void PrintFloat(const CallInstruction &call) {
    if (target_os != "darwin" && target_os != "macos" && target_os != "linux")
      Fail("print_f64 currently requires Darwin or Linux");
    if (call.operands.size() != 1 || !Type(call.operands[0]).IsFloat())
      Fail("print_f64 expects one floating-point number");
    Load(call.operands[0], 9);
    if (Type(call.operands[0]).kind == IRTypeKind::kF32) {
      Emit(0x1E270120u); // fmov s0,w9
      Emit(0x1E22C000u); // fcvt d0,s0
      Emit(0x9E660009u); // fmov x9,d0
    }
    for (auto instruction : native::kPrintF64) {
      if (target_os == "linux") {
        if (instruction == 0xD2800090u) instruction = 0xD2800808u; // mov x8,#64
        if (instruction == 0xD4001001u) instruction = 0xD4000001u; // svc #0
      }
      Emit(instruction);
    }
  }
  void PythonTrueDivision(const CallInstruction &call) {
    if (call.operands.size() != 2) Fail("Python true division expects two operands");
    for (unsigned i = 0; i < 2; ++i) {
      Load(call.operands[i], 9);
      const auto type = Type(call.operands[i]);
      if (type.kind == IRTypeKind::kF64) Emit(0x9E670000u | (9 << 5) | i);
      else if (type.IsInteger()) Emit(0x9E620000u | (9 << 5) | i); // scvtf dN,x9
      else Fail("Python true division requires integer or binary64 operands");
    }
    Emit(0x1E602028u); // fcmp d1,#0.0
    Emit(0x54000041u); // b.ne nonzero
    Emit(0xD4200000u); // division by zero has no native exception runtime
    Emit(0x1E611800u); // fdiv d0,d0,d1
    Emit(0x9E660009u); // fmov x9,d0
    Save(call);
  }
  void InstructionCode(const Instruction &inst, const BasicBlock *block) {
    if (auto c = dynamic_cast<const ConstantInstruction *>(&inst)) {
      Imm(9, c->bits); Save(inst);
    } else if (auto b = dynamic_cast<const BinaryInstruction *>(&inst)) Binary(*b);
    else if (dynamic_cast<const AllocaInstruction *>(&inst)) {
      Address(9, objects.at(inst.name)); Save(inst);
    } else if (dynamic_cast<const AssignInstruction *>(&inst)) {
      Load(inst.operands.at(0), 9); Save(inst);
    } else if (auto c = dynamic_cast<const CastInstruction *>(&inst)) {
      Load(c->operands.at(0), 9);
      if (c->cast == CastInstruction::CastKind::kZExt || c->cast == CastInstruction::CastKind::kSExt) {
        auto source = Type(c->operands[0]);
        source.is_signed = c->cast == CastInstruction::CastKind::kSExt;
        Normalize(9, source);
      }
      if (c->cast == CastInstruction::CastKind::kFpExt || c->cast == CastInstruction::CastKind::kFpTrunc) {
        const bool extend = c->cast == CastInstruction::CastKind::kFpExt;
        Emit((extend ? 0x1E270000u : 0x9E670000u) | (9 << 5));
        Emit(extend ? 0x1E22C000u : 0x1E624000u);
        Emit((extend ? 0x9E660000u : 0x1E260000u) | 9);
      }
      Save(inst);
    } else if (dynamic_cast<const LoadInstruction *>(&inst)) {
      Load(inst.operands.at(0), 10);
      auto size = module.Layout().SizeOf(inst.type);
      std::uint32_t base = size == 1 ? 0x39400000u : size == 2 ? 0x79400000u : size == 4 ? 0xB9400000u : 0xF9400000u;
      if (size != 1 && size != 2 && size != 4 && size != 8) Fail("aggregate load");
      Emit(base | (10 << 5) | 9); Save(inst);
    } else if (dynamic_cast<const StoreInstruction *>(&inst)) {
      Load(inst.operands.at(0), 10); Load(inst.operands.at(1), 9);
      auto pointer = Type(inst.operands[0]);
      if (pointer.subtypes.empty()) Fail("store without pointee type");
      auto size = module.Layout().SizeOf(pointer.subtypes.front());
      if (size != 1 && size != 2 && size != 4 && size != 8) Fail("aggregate store");
      Emit((size == 1 ? 0x39000000u : size == 2 ? 0x79000000u : size == 4 ? 0xB9000000u : 0xF9000000u) | (10 << 5) | 9);
    } else if (auto gep = dynamic_cast<const GetElementPtrInstruction *>(&inst)) {
      Load(inst.operands.at(0), 9);
      IRType type = gep->source_type; std::size_t offset = 0;
      for (auto index : gep->indices) {
        if (type.kind == IRTypeKind::kStruct) {
          if (index >= type.subtypes.size()) Fail("GEP field out of range");
          std::size_t field_offset = 0;
          for (std::size_t i = 0; i <= index; ++i) {
            auto align = module.Layout().AlignOf(type.subtypes[i]);
            field_offset = (field_offset + align - 1) / align * align;
            if (i < index) field_offset += module.Layout().SizeOf(type.subtypes[i]);
          }
          offset += field_offset; type = IRType(type.subtypes[index]);
        } else if (!type.subtypes.empty()) {
          type = IRType(type.subtypes[0]); offset += index * module.Layout().SizeOf(type);
        } else Fail("unsupported GEP type");
      }
      Imm(10, offset); Emit(0x8B0A0129u); Save(inst);
    } else if (auto call = dynamic_cast<const CallInstruction *>(&inst)) {
      if (call->callee == "polyrt_println") { Println(*call); return; }
      if (call->callee == "polyrt_print_i64") { PrintInteger(*call); return; }
      if (call->callee == "polyrt_print_f64") { PrintFloat(*call); return; }
      if (call->callee == "__py_true_div") { PythonTrueDivision(*call); return; }
      if (call->is_vararg) Fail("variadic call ABI is not supported");
      unsigned integer = 0, floating = 0;
      const Function *callee = module.FindFunction(call->callee);
      for (std::size_t i = 0; i < call->operands.size(); ++i) {
        auto type = callee && i < callee->param_types.size() ? callee->param_types[i] : Type(call->operands[i]);
        if (type.IsFloat()) {
          if (floating >= 8) Fail("more than eight floating arguments");
          Load(call->operands[i], 9);
          Emit((type.kind == IRTypeKind::kF32 ? 0x1E270000u : 0x9E670000u) | (9 << 5) | floating++);
        } else {
          if (integer >= 8) Fail("more than eight integer arguments");
          Load(call->operands[i], integer++);
        }
      }
      if (call->is_indirect) { Load(call->callee, 16); Emit(0xD63F0200u); }
      else { calls.push_back({code.size(), call->callee}); Emit(0x94000000u); }
      if (inst.type.IsFloat()) Emit((inst.type.kind == IRTypeKind::kF32 ? 0x1E260000u : 0x9E660000u) | 9);
      else Emit(0xAA0003E9u); // mov x9,x0
      Save(inst);
    } else if (dynamic_cast<const ReturnStatement *>(&inst)) {
      if (inst.operands.empty()) Imm(0, 0);
      else {
        Load(inst.operands[0], 0);
        if (function->ret_type.IsFloat())
          Emit(function->ret_type.kind == IRTypeKind::kF32 ? 0x1E270000u : 0x9E670000u);
        else Normalize(0, function->ret_type);
      }
      Epilogue();
    } else if (auto br = dynamic_cast<const BranchStatement *>(&inst)) Edge(block, br->target);
    else if (auto br = dynamic_cast<const CondBranchStatement *>(&inst)) {
      Load(inst.operands.at(0), 9);
      auto at = code.size(); Emit(0xB4000009u); // cbz x9,false edge
      Edge(block, br->true_target);
      auto distance = (code.size() - at) / 4;
      if (distance >= (1 << 18)) Fail("conditional edge exceeds range");
      Patch(at, 0xB4000009u | (distance << 5));
      Edge(block, br->false_target);
    } else if (auto sw = dynamic_cast<const SwitchStatement *>(&inst)) {
      for (const auto &item : sw->cases) {
        Load(inst.operands.at(0), 9); Imm(10, item.value); Emit(0xEB0A013Fu);
        auto at = code.size(); Emit(0x54000001u); // b.ne next
        Edge(block, item.target);
        Patch(at, 0x54000001u | (((code.size() - at) / 4) << 5));
      }
      Edge(block, sw->default_target);
    } else if (dynamic_cast<const UnreachableStatement *>(&inst)) Emit(0xD4200000u);
    else if (!dynamic_cast<const PhiInstruction *>(&inst)) Fail("unsupported IR instruction in " + function->name);
  }
  void FunctionCode(const Function &fn) {
    if (fn.is_external) return;
    if (fn.is_bridge_stub) Fail("precompiled bridge ABI is not supported");
    if (fn.blocks.empty()) Fail("empty function '" + fn.name + "'");
    function = &fn; slots.clear(); objects.clear(); types.clear(); blocks.clear(); jumps.clear(); extent = 0;
    registers = native::AllocateRegisters(fn, allocation_strategy);
    saved_registers.clear();
    for (const auto &[name, reg] : registers) saved_registers.emplace(reg, 0);
    for (auto &[reg, offset] : saved_registers) offset = Allocate();
    for (std::size_t i = 0; i < fn.params.size(); ++i) Define(fn.params[i], fn.param_types.at(i));
    std::size_t max_phis = 0;
    for (const auto &bb : fn.blocks) {
      max_phis = std::max(max_phis, bb->phis.size());
      for (const auto &phi : bb->phis) Define(phi->name, phi->type);
      for (const auto &inst : bb->instructions) {
        if (inst->HasResult()) Define(inst->name, inst->type);
        if (dynamic_cast<const AllocaInstruction *>(inst.get())) {
          if (inst->type.subtypes.empty()) Fail("alloca without element type");
          const auto &t = inst->type.subtypes.front();
          objects[inst->name] = Allocate(std::max<std::size_t>(1, module.Layout().SizeOf(t)),
                                        std::max<std::size_t>(8, module.Layout().AlignOf(t)));
        }
      }
    }
    phi_scratch = Allocate(max_phis * 8); frame = (extent + 15) & ~std::size_t(15);
    const auto start = code.size(); functions[fn.name] = start;
    Emit(0xA9BF7BFDu); Emit(0x910003FDu);
    if (frame) {
      if (frame <= 4095) Emit(0xD10003FFu | (frame << 10));
      else { Imm(17, frame); Emit(0xCB3163FFu); }
    }
    for (const auto &[reg, offset] : saved_registers) Slot(reg, offset, true);
    unsigned integer = 0, floating = 0;
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
      if (fn.param_types[i].IsFloat()) {
        if (floating >= 8) Fail("more than eight floating parameters");
        Emit((fn.param_types[i].kind == IRTypeKind::kF32 ? 0x1E260000u : 0x9E660000u) | (floating++ << 5) | 9);
        StoreValue(fn.params[i], 9);
      } else {
        if (integer >= 8) Fail("more than eight integer parameters");
        Normalize(integer, fn.param_types[i]); StoreValue(fn.params[i], integer++);
      }
    }
    const BasicBlock *entry = fn.entry ? fn.entry : fn.blocks.front().get();
    if (entry != fn.blocks.front().get()) { jumps.push_back({code.size(), entry}); Emit(0x14000000u); }
    for (const auto &bb : fn.blocks) {
      blocks[bb.get()] = code.size();
      for (const auto &inst : bb->instructions) InstructionCode(*inst, bb.get());
      if (bb->terminator) InstructionCode(*bb->terminator, bb.get());
      else Fail("unterminated block '" + bb->name + "'");
    }
    for (const auto &jump : jumps) BranchAt(jump.offset, blocks.at(jump.target));
    result.symbols.push_back({fn.name, ".text", start, code.size() - start, true, true});
  }
public:
  explicit NativeEmitter(const IRContext &ctx, std::string os, RegAllocStrategy strategy)
      : module(ctx), target_os(std::move(os)), allocation_strategy(strategy) {}
  Result Run() {
    for (const auto &fn : module.Functions()) FunctionCode(*fn);
    for (const auto &call : calls) {
      auto it = functions.find(call.name);
      if (it != functions.end()) BranchAt(call.offset, it->second, true);
      else {
        result.relocs.push_back({".text", static_cast<std::uint32_t>(call.offset), 1, call.name, 0});
        if (std::none_of(result.symbols.begin(), result.symbols.end(), [&](const auto &s) { return s.name == call.name; }))
          result.symbols.push_back({call.name, "", 0, 0, true, false});
      }
    }
    result.sections.push_back({".text", std::move(code), false});
    Arm64Target::MCSection rodata{".rdata", {}, false};
    for (const auto &g : module.Globals()) {
      auto str = std::dynamic_pointer_cast<ConstantString>(g->initializer);
      if (!str) continue;
      auto offset = rodata.data.size();
      rodata.data.insert(rodata.data.end(), str->data.begin(), str->data.end());
      if (str->null_terminated) rodata.data.push_back(0);
      result.symbols.push_back({g->name, ".rdata", offset, rodata.data.size() - offset, true, true});
    }
    for (const auto &g : module.Globals()) {
      auto gep = std::dynamic_pointer_cast<ConstantGEP>(g->initializer);
      if (!gep) continue;
      auto base = std::dynamic_pointer_cast<GlobalValue>(gep->base);
      if (!base) Fail("unsupported global GEP");
      auto offset = rodata.data.size(); rodata.data.resize(offset + 8);
      result.symbols.push_back({g->name, ".rdata", offset, 8, true, true});
      result.relocs.push_back({".rdata", static_cast<std::uint32_t>(offset), 0, base->name, 0});
    }
    if (!rodata.data.empty()) result.sections.push_back(std::move(rodata));
    Arm64Target::MCSection data{".data", {}, false};
    for (const auto &global : module.Globals()) {
      auto literal = std::dynamic_pointer_cast<LiteralExpression>(global->initializer);
      if (!literal && global->initializer) continue;
      const auto size = module.Layout().SizeOf(global->type);
      if (size != 1 && size != 2 && size != 4 && size != 8)
        Fail("unsupported scalar global initializer for '" + global->name + "'");
      const auto alignment = module.Layout().AlignOf(global->type);
      while (data.data.size() % alignment) data.data.push_back(0);
      const auto offset = data.data.size();
      std::uint64_t bits = literal ? static_cast<std::uint64_t>(literal->i64) : 0;
      if (literal && literal->is_float) {
        if (size == 4) { float value = static_cast<float>(literal->f64); std::memcpy(&bits, &value, sizeof(value)); }
        else std::memcpy(&bits, &literal->f64, sizeof(bits));
      }
      for (std::size_t byte = 0; byte < size; ++byte) data.data.push_back((bits >> (8 * byte)) & 255);
      result.symbols.push_back({global->name, ".data", offset, size, true, true});
    }
    if (!data.data.empty()) result.sections.push_back(std::move(data));
    return std::move(result);
  }
};
} // namespace
Arm64Target::MCResult Arm64Target::EmitObjectCode() {
  if (!module_) return {};
  return NativeEmitter(*module_, target_os_, regalloc_strategy_).Run();
}
} // namespace polyglot::backends::arm64
