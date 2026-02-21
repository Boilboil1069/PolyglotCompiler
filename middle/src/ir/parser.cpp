#include "common/include/ir/ir_parser.h"

#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "common/include/ir/ir_printer.h"

namespace polyglot::ir {
namespace {

std::string Trim(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

bool StartsWith(const std::string &s, const std::string &p) { return s.rfind(p, 0) == 0; }

IRType ParseType(const std::string &text);

IRType ParseBaseType(const std::string &text) {
  const std::string t = Trim(text);
  if (t == "i1") return IRType::I1();
  if (t == "i8") return IRType::I8(true);
  if (t == "u8") return IRType::I8(false);
  if (t == "i16") return IRType::I16(true);
  if (t == "u16") return IRType::I16(false);
  if (t == "i32") return IRType::I32(true);
  if (t == "u32") return IRType::I32(false);
  if (t == "i64") return IRType::I64(true);
  if (t == "u64") return IRType::I64(false);
  if (t == "f32") return IRType::F32();
  if (t == "f64") return IRType::F64();
  if (t == "void") return IRType::Void();
  if (t == "invalid") return IRType::Invalid();
  // function type: ret (params)
  auto lp = t.find('(');
  auto rp = t.rfind(')');
  if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
    IRType ret = ParseType(t.substr(0, lp));
    std::vector<IRType> params;
    std::string inside = t.substr(lp + 1, rp - lp - 1);
    std::stringstream ss(inside);
    std::string item;
    while (std::getline(ss, item, ',')) {
      item = Trim(item);
      if (item.empty()) continue;
      params.push_back(ParseType(item));
    }
    return IRType::Function(ret, params);
  }
  // struct: %name {t0, t1}
  if (!t.empty() && t[0] == '%') {
    auto brace = t.find('{');
    std::string name = t.substr(1, brace == std::string::npos ? std::string::npos : brace - 1);
    std::vector<IRType> fields;
    if (brace != std::string::npos) {
      auto close = t.rfind('}');
      std::string body = close == std::string::npos ? t.substr(brace + 1) : t.substr(brace + 1, close - brace - 1);
      std::stringstream ss(body);
      std::string item;
      while (std::getline(ss, item, ',')) {
        item = Trim(item);
        if (!item.empty()) fields.push_back(ParseType(item));
      }
    }
    return IRType::Struct(name, fields);
  }
  return IRType{IRTypeKind::kInvalid, t};
}

IRType ParseType(const std::string &text) {
  std::string t = Trim(text);
  // Handle pointer/reference suffixes
  size_t stars = 0;
  size_t refs = 0;
  while (!t.empty() && (t.back() == '*' || t.back() == '&')) {
    if (t.back() == '*') ++stars; else ++refs;
    t.pop_back();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
  }

  if (!t.empty() && t.front() == '[') {
    // array: [N x T]
    auto x = t.find('x');
    auto close = t.rfind(']');
    size_t n = static_cast<size_t>(std::stoul(t.substr(1, x - 1)));
    IRType elem = ParseType(t.substr(x + 1, close - x - 1));
    IRType arr = IRType::Array(elem, n);
    for (size_t i = 0; i < stars; ++i) arr = IRType::Pointer(arr);
    for (size_t i = 0; i < refs; ++i) arr = IRType::Reference(arr);
    return arr;
  }
  if (!t.empty() && t.front() == '<') {
    // vector: <N x T>
    auto x = t.find('x');
    auto close = t.rfind('>');
    size_t n = static_cast<size_t>(std::stoul(t.substr(1, x - 1)));
    IRType elem = ParseType(t.substr(x + 1, close - x - 1));
    IRType vec = IRType::Vector(elem, n);
    for (size_t i = 0; i < stars; ++i) vec = IRType::Pointer(vec);
    for (size_t i = 0; i < refs; ++i) vec = IRType::Reference(vec);
    return vec;
  }

  IRType base = ParseBaseType(t);
  for (size_t i = 0; i < stars; ++i) base = IRType::Pointer(base);
  for (size_t i = 0; i < refs; ++i) base = IRType::Reference(base);
  return base;
}

std::vector<std::string> SplitOperands(const std::string &s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = Trim(item);
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

BinaryInstruction::Op ParseBinOp(const std::string &s) {
  if (s == "add") return BinaryInstruction::Op::kAdd;
  if (s == "sub") return BinaryInstruction::Op::kSub;
  if (s == "mul") return BinaryInstruction::Op::kMul;
  if (s == "sdiv") return BinaryInstruction::Op::kSDiv;
  if (s == "udiv") return BinaryInstruction::Op::kUDiv;
  if (s == "srem") return BinaryInstruction::Op::kSRem;
  if (s == "urem") return BinaryInstruction::Op::kURem;
  if (s == "and") return BinaryInstruction::Op::kAnd;
  if (s == "or") return BinaryInstruction::Op::kOr;
  if (s == "xor") return BinaryInstruction::Op::kXor;
  if (s == "shl") return BinaryInstruction::Op::kShl;
  if (s == "lshr") return BinaryInstruction::Op::kLShr;
  if (s == "ashr") return BinaryInstruction::Op::kAShr;
  if (s == "cmpeq") return BinaryInstruction::Op::kCmpEq;
  if (s == "cmpne") return BinaryInstruction::Op::kCmpNe;
  if (s == "cmpult") return BinaryInstruction::Op::kCmpUlt;
  if (s == "cmpule") return BinaryInstruction::Op::kCmpUle;
  if (s == "cmpugt") return BinaryInstruction::Op::kCmpUgt;
  if (s == "cmpuge") return BinaryInstruction::Op::kCmpUge;
  if (s == "cmpslt") return BinaryInstruction::Op::kCmpSlt;
  if (s == "cmpsle") return BinaryInstruction::Op::kCmpSle;
  if (s == "cmpsgt") return BinaryInstruction::Op::kCmpSgt;
  if (s == "cmpsge") return BinaryInstruction::Op::kCmpSge;
  if (s == "cmpfoe") return BinaryInstruction::Op::kCmpFoe;
  if (s == "cmpfne") return BinaryInstruction::Op::kCmpFne;
  if (s == "cmpflt") return BinaryInstruction::Op::kCmpFlt;
  if (s == "cmpfle") return BinaryInstruction::Op::kCmpFle;
  if (s == "cmpfgt") return BinaryInstruction::Op::kCmpFgt;
  if (s == "cmpfge") return BinaryInstruction::Op::kCmpFge;
  return BinaryInstruction::Op::kAdd;
}

CastInstruction::CastKind ParseCast(const std::string &s) {
  if (s == "zext") return CastInstruction::CastKind::kZExt;
  if (s == "sext") return CastInstruction::CastKind::kSExt;
  if (s == "trunc") return CastInstruction::CastKind::kTrunc;
  if (s == "bitcast") return CastInstruction::CastKind::kBitcast;
  if (s == "fpext") return CastInstruction::CastKind::kFpExt;
  if (s == "fptrunc") return CastInstruction::CastKind::kFpTrunc;
  if (s == "inttoptr") return CastInstruction::CastKind::kIntToPtr;
  if (s == "ptrtoint") return CastInstruction::CastKind::kPtrToInt;
  return CastInstruction::CastKind::kBitcast;
}

struct PendingPhi {
  std::shared_ptr<PhiInstruction> phi;
  std::vector<std::string> preds;
  std::vector<std::string> vals;
};

struct PendingBranch {
  BranchStatement *br;
  std::string target;
};

struct PendingCondBranch {
  CondBranchStatement *br;
  std::string t;
  std::string f;
};

struct PendingSwitch {
  SwitchStatement *sw;
  std::vector<std::string> case_blocks;
  std::string default_block;
};

bool ParseFunctionBody(const std::vector<std::string> &lines, IRContext &ctx, std::shared_ptr<Function> &fn, std::string *msg) {
  std::unordered_map<std::string, std::shared_ptr<BasicBlock>> blocks;
  std::unordered_map<std::string, IRType> value_types;
  std::shared_ptr<BasicBlock> current_bb;
  std::vector<PendingPhi> pending_phi;
  std::vector<PendingBranch> pending_br;
  std::vector<PendingCondBranch> pending_cbr;
  std::vector<PendingSwitch> pending_sw;

  auto get_or_create_bb = [&](const std::string &name) -> std::shared_ptr<BasicBlock> {
    auto it = blocks.find(name);
    if (it != blocks.end()) return it->second;
    auto bb = std::make_shared<BasicBlock>();
    bb->name = name;
    blocks[name] = bb;
    fn->blocks.push_back(bb);
    if (!fn->entry) fn->entry = bb.get();
    return bb;
  };

  for (size_t i = 0; i < fn->params.size(); ++i) {
    IRType pt = (i < fn->param_types.size()) ? fn->param_types[i] : IRType::Invalid();
    value_types[fn->params[i]] = pt;
  }

  for (const auto &raw : lines) {
    const std::string line = Trim(raw);
    if (line.empty()) continue;
    if (line.back() == ':') {
      std::string bbname = line.substr(0, line.size() - 1);
      current_bb = get_or_create_bb(bbname);
      continue;
    }
    if (!current_bb) {
      if (msg) *msg = "instruction without basic block";
      return false;
    }

    // split type suffix
    auto colon = line.rfind(" : ");
    if (colon == std::string::npos) {
      if (msg) *msg = "missing type in instruction";
      return false;
    }
    std::string inst_part = line.substr(0, colon);
    std::string type_part = Trim(line.substr(colon + 3));
    IRType ty = ParseType(type_part);

    std::string lhs;
    auto eq = inst_part.find('=');
    if (eq != std::string::npos) {
      lhs = Trim(inst_part.substr(0, eq));
      inst_part = Trim(inst_part.substr(eq + 1));
    }

    auto take_align = [&](size_t &align_out) {
      align_out = 0;
      auto pos = inst_part.rfind(" align ");
      if (pos != std::string::npos) {
        align_out = static_cast<size_t>(std::stoul(inst_part.substr(pos + 7)));
        inst_part = Trim(inst_part.substr(0, pos));
      }
    };

    std::shared_ptr<Instruction> new_inst;

    if (StartsWith(inst_part, "phi")) {
      auto phi = std::make_shared<PhiInstruction>();
      phi->name = lhs;
      phi->type = ty;
      std::string list = Trim(inst_part.substr(3));
      // list format: [pred: val], [pred: val]
      std::vector<std::string> entries;
      std::stringstream ss(list);
      std::string item;
      while (std::getline(ss, item, ']')) {
        if (item.empty()) continue;
        auto lb = item.find('[');
        if (lb == std::string::npos) continue;
        std::string body = Trim(item.substr(lb + 1));
        if (body.empty()) continue;
        auto colon_pos = body.find(':');
        std::string pred = Trim(body.substr(0, colon_pos));
        std::string val = Trim(body.substr(colon_pos + 1));
        phi->incomings.push_back({nullptr, val});
        pending_phi.push_back({phi, {pred}, {val}});
      }
      current_bb->AddPhi(phi);
      new_inst = phi;
      if (phi->HasResult()) value_types[phi->name] = phi->type;
    } else if (StartsWith(inst_part, "call")) {
      auto call = std::make_shared<CallInstruction>();
      std::string rest = Trim(inst_part.substr(4));
      bool indirect = false;
      if (!rest.empty() && rest[0] == '*') { indirect = true; rest = rest.substr(1); }
      auto lp = rest.find('(');
      // Find matching ')' for the call arguments (balanced parentheses)
      size_t rp = std::string::npos;
      if (lp != std::string::npos) {
        int depth = 1;
        for (size_t k = lp + 1; k < rest.size(); ++k) {
          if (rest[k] == '(') ++depth;
          else if (rest[k] == ')') { --depth; if (depth == 0) { rp = k; break; } }
        }
      }
      call->callee = Trim(rest.substr(0, lp));
      std::string args = rp == std::string::npos ? std::string() : rest.substr(lp + 1, rp - lp - 1);
      call->operands = SplitOperands(args);
      std::string tail = rp == std::string::npos ? std::string() : Trim(rest.substr(rp + 1));
      if (!tail.empty() && tail.front() == '[' && tail.back() == ']') {
        std::string body = Trim(tail.substr(1, tail.size() - 2));
        if (StartsWith(body, "fn")) {
          body = Trim(body.substr(2));
          // check vararg suffix
          bool vararg = false;
          auto var_pos = body.rfind("vararg");
          if (var_pos != std::string::npos) {
            vararg = true;
            body = Trim(body.substr(0, var_pos));
          }
          call->callee_type = ParseType(body);
          call->is_vararg = vararg;
        } else if (body == "vararg") {
          call->is_vararg = true;
        }
      }
      call->is_indirect = indirect;
      call->type = ty;
      call->name = ty.kind == IRTypeKind::kVoid ? "" : lhs;
      new_inst = call;
      if (call->HasResult()) value_types[call->name] = call->type;
    } else if (StartsWith(inst_part, "alloca")) {
      auto al = std::make_shared<AllocaInstruction>();
      al->name = lhs;
      al->type = ty;
      new_inst = al;
      if (al->HasResult()) value_types[al->name] = al->type;
    } else if (StartsWith(inst_part, "load")) {
      auto ld = std::make_shared<LoadInstruction>();
      take_align(ld->align);
      std::string ops = Trim(inst_part.substr(4));
      ld->operands = SplitOperands(ops);
      ld->name = lhs;
      ld->type = ty;
      new_inst = ld;
      if (ld->HasResult()) value_types[ld->name] = ld->type;
    } else if (StartsWith(inst_part, "store")) {
      auto st = std::make_shared<StoreInstruction>();
      take_align(st->align);
      std::string ops = Trim(inst_part.substr(5));
      st->operands = SplitOperands(ops);
      st->type = ty;
      new_inst = st;
    } else if (StartsWith(inst_part, "gep")) {
      auto gep = std::make_shared<GetElementPtrInstruction>();
      std::string rest = Trim(inst_part.substr(3));
      auto lb = rest.find('[');
      std::string base = Trim(rest.substr(0, lb));
      auto rb = rest.find(']');
      std::string idxs = rest.substr(lb + 1, rb - lb - 1);
      gep->operands = {base};
      std::stringstream ss(idxs);
      std::string item;
      while (std::getline(ss, item, ',')) {
        item = Trim(item);
        if (!item.empty()) gep->indices.push_back(static_cast<size_t>(std::stoul(item)));
      }
      std::string tail = Trim(rest.substr(rb + 1));
      if (StartsWith(tail, "inbounds")) gep->inbounds = true;
      gep->name = lhs;
      gep->type = ty;
      auto it_base = value_types.find(base);
      if (it_base != value_types.end() && !it_base->second.subtypes.empty()) {
        gep->source_type = it_base->second.subtypes[0];
      }
      new_inst = gep;
      if (gep->HasResult()) value_types[gep->name] = gep->type;
    } else if (StartsWith(inst_part, "ret")) {
      auto ret = std::make_shared<ReturnStatement>();
      std::string ops = Trim(inst_part.substr(3));
      if (!ops.empty()) ret->operands = {ops};
      ret->type = ty;
      new_inst = ret;
      current_bb->SetTerminator(ret);
    } else if (StartsWith(inst_part, "br")) {
      auto br = std::make_shared<BranchStatement>();
      std::string tgt = Trim(inst_part.substr(4));  // "-> label"
      if (StartsWith(tgt, "->")) tgt = Trim(tgt.substr(2));
      br->type = ty;
      pending_br.push_back({br.get(), tgt});
      new_inst = br;
      current_bb->SetTerminator(br);
    } else if (StartsWith(inst_part, "cbr")) {
      auto cbr = std::make_shared<CondBranchStatement>();
      std::string rest = Trim(inst_part.substr(4));
      auto qm = rest.find('?');
      auto colon = rest.find(':', qm);
      std::string cond = Trim(rest.substr(0, qm));
      std::string t = Trim(rest.substr(qm + 1, colon - qm - 1));
      std::string f = Trim(rest.substr(colon + 1));
      cbr->operands = {cond};
      cbr->type = ty;
      pending_cbr.push_back({cbr.get(), t, f});
      new_inst = cbr;
      current_bb->SetTerminator(cbr);
    } else if (StartsWith(inst_part, "switch")) {
      auto sw = std::make_shared<SwitchStatement>();
      std::string rest = Trim(inst_part.substr(6));
      auto lb = rest.find('{');
      std::string scrut = Trim(rest.substr(0, lb));
      sw->operands = {scrut};
      std::string body = rest.substr(lb + 1, rest.rfind('}') - lb - 1);
      std::stringstream ss(body);
      std::string item;
      std::vector<std::string> block_names;
      while (ss >> item) {
        if (item == "default") break;
        long long value = std::stoll(item);
        std::string arrow;
        ss >> arrow; // ->
        std::string target;
        ss >> target;
        sw->cases.push_back({value, nullptr});
        block_names.push_back(target);
      }
      std::string arrow, def;
      ss >> arrow >> def; // default -> name
      PendingSwitch pend{sw.get(), block_names, def};
      pending_sw.push_back(pend);
      sw->type = ty;
      new_inst = sw;
      current_bb->SetTerminator(sw);
    } else if (StartsWith(inst_part, "memcpy")) {
      auto mc = std::make_shared<MemcpyInstruction>();
      take_align(mc->align);
      std::string ops = Trim(inst_part.substr(6));
      mc->operands = SplitOperands(ops);
      mc->type = ty;
      new_inst = mc;
    } else if (StartsWith(inst_part, "memset")) {
      auto ms = std::make_shared<MemsetInstruction>();
      take_align(ms->align);
      std::string ops = Trim(inst_part.substr(6));
      ms->operands = SplitOperands(ops);
      ms->type = ty;
      new_inst = ms;
    } else if (StartsWith(inst_part, "unreachable")) {
      auto ur = std::make_shared<UnreachableStatement>();
      ur->type = ty;
      new_inst = ur;
      current_bb->SetTerminator(ur);
    } else {
      // assume binary op
      std::stringstream ss(inst_part);
      std::string op_name;
      ss >> op_name;
      std::string rest;
      std::getline(ss, rest);
      auto bin = std::make_shared<BinaryInstruction>();
      bin->op = ParseBinOp(op_name);
      bin->operands = SplitOperands(rest);
      bin->name = lhs;
      bin->type = ty;
      new_inst = bin;
      if (bin->HasResult()) value_types[bin->name] = bin->type;
    }

    if (new_inst && !new_inst->IsTerminator()) {
      new_inst->parent = current_bb.get();
      current_bb->AddInstruction(new_inst);
    }
  }

  // resolve references
  for (auto &p : pending_phi) {
    auto phi = p.phi;
    phi->incomings.clear();
    for (size_t i = 0; i < p.preds.size(); ++i) {
      auto it = blocks.find(p.preds[i]);
      phi->incomings.push_back({it == blocks.end() ? nullptr : it->second.get(), p.vals[i]});
    }
  }
  for (auto &p : pending_br) {
    auto it = blocks.find(p.target);
    p.br->target = it == blocks.end() ? nullptr : it->second.get();
  }
  for (auto &p : pending_cbr) {
    auto it_t = blocks.find(p.t);
    auto it_f = blocks.find(p.f);
    p.br->true_target = it_t == blocks.end() ? nullptr : it_t->second.get();
    p.br->false_target = it_f == blocks.end() ? nullptr : it_f->second.get();
  }
  for (auto &p : pending_sw) {
    for (size_t i = 0; i < p.case_blocks.size() && i < p.sw->cases.size(); ++i) {
      auto it = blocks.find(p.case_blocks[i]);
      p.sw->cases[i].target = it == blocks.end() ? nullptr : it->second.get();
    }
    auto it = blocks.find(p.default_block);
    p.sw->default_target = it == blocks.end() ? nullptr : it->second.get();
  }

  return true;
}

}  // namespace

bool ParseFunction(const std::string &text, IRContext &ctx, std::shared_ptr<Function> *out_fn, std::string *msg) {
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line)) return false;
  line = Trim(line);
  if (!StartsWith(line, "func")) return false;
  auto name_start = line.find(' ');
  auto lp = line.find('(');
  auto rp = line.rfind(')');
  std::string fname = Trim(line.substr(name_start + 1, lp - name_start - 1));
  std::string params_str = line.substr(lp + 1, rp - lp - 1);

  std::vector<std::pair<std::string, IRType>> params;
  std::stringstream pss(params_str);
  std::string pitem;
  while (std::getline(pss, pitem, ',')) {
    pitem = Trim(pitem);
    if (pitem.empty()) continue;
    params.push_back({pitem, IRType::Invalid()});
  }

  auto fn = ctx.CreateFunction(fname, IRType::Void(), params);
  std::vector<std::string> body_lines;
  while (std::getline(in, line)) {
    body_lines.push_back(line);
  }
  if (!ParseFunctionBody(body_lines, ctx, fn, msg)) return false;

  // Infer return type from the first 'ret' instruction if the function was
  // created with Void but actually returns a value.
  if (fn->ret_type.kind == IRTypeKind::kVoid) {
    for (auto &bb : fn->blocks) {
      if (bb->terminator) {
        if (auto *ret = dynamic_cast<ReturnStatement *>(bb->terminator.get())) {
          if (ret->type.kind != IRTypeKind::kVoid) {
            fn->ret_type = ret->type;
            break;
          }
        }
      }
    }
  }

  if (out_fn) *out_fn = fn;
  return true;
}

bool ParseModule(const std::string &text, IRContext &ctx, std::string *msg) {
  std::istringstream in(text);
  std::string line;
  std::string current_func_text;
  bool in_func = false;
  while (std::getline(in, line)) {
    if (StartsWith(Trim(line), "func")) {
      if (in_func) {
        if (!ParseFunction(current_func_text, ctx, nullptr, msg)) return false;
        current_func_text.clear();
      }
      in_func = true;
    }
    if (in_func) {
      current_func_text += line + "\n";
    }
  }
  if (in_func && !current_func_text.empty()) {
    if (!ParseFunction(current_func_text, ctx, nullptr, msg)) return false;
  }
  return true;
}

}  // namespace polyglot::ir
