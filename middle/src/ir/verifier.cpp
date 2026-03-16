#include "middle/include/ir/verifier.h"

#include <algorithm>
#include <queue>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/data_layout.h"

namespace polyglot::ir {

namespace {
bool IsPowerOfTwo(size_t v) { return v && ((v & (v - 1)) == 0); }

bool Fail(const std::string &reason, std::string *msg) {
	if (msg) *msg = reason;
	return false;
}

bool IsImmediate(const std::string &name) {
	if (name.empty()) return false;
	char *end = nullptr;
	(void)std::strtoll(name.c_str(), &end, 0);
	return end && end != name.c_str() && *end == '\0';
}

bool IsLiteralConstant(const std::string &name) {
	// IRBuilder names integer literals as c<N> and float literals as cf<N>.
	if (name.size() < 2 || name[0] != 'c') return false;
	size_t start = 1;
	if (name[1] == 'f') start = 2;  // float prefix "cf"
	if (start >= name.size()) return false;
	for (size_t i = start; i < name.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
	}
	return true;
}

bool IsDefined(const std::string &name, const std::unordered_set<std::string> &defs,
               const std::unordered_set<std::string> *extra = nullptr) {
	if (name.empty()) return true;  // allow empty/literal operands
	if (name == "undef") return true;  // SSA undef placeholder
	if (IsImmediate(name)) return true;
	if (IsLiteralConstant(name)) return true;
	if (defs.count(name) > 0) return true;
	if (extra && extra->count(name) > 0) return true;
	return false;
}

IRType LookupType(const std::string &name, const std::unordered_map<std::string, IRType> &types) {
	if (IsImmediate(name)) return IRType::I64(true);
	auto it = types.find(name);
	return it == types.end() ? IRType::Invalid() : it->second;
}

size_t NaturalAlign(const IRType &type, const DataLayout *layout) {
	if (layout) return layout->AlignOf(type);
	switch (type.kind) {
		case IRTypeKind::kI1:
		case IRTypeKind::kI8: return 1;
		case IRTypeKind::kI16: return 2;
		case IRTypeKind::kI32:
		case IRTypeKind::kF32: return 4;
		case IRTypeKind::kI64:
		case IRTypeKind::kF64: return 8;
		case IRTypeKind::kPointer:
		case IRTypeKind::kReference: return 8;
		case IRTypeKind::kArray:
		case IRTypeKind::kVector: {
			if (type.subtypes.empty()) return 1;
			return NaturalAlign(type.subtypes[0], layout);
		}
		case IRTypeKind::kStruct: {
			size_t max_align = 1;
			for (auto &f : type.subtypes) {
				max_align = std::max(max_align, NaturalAlign(f, layout));
			}
			return max_align == 0 ? 1 : max_align;
		}
		case IRTypeKind::kFunction:
		case IRTypeKind::kInvalid:
		case IRTypeKind::kVoid:
		default:
			return 1;
	}
}

bool CheckBinary(const BinaryInstruction &bin, const IRType &lhs, const IRType &rhs, std::string *msg) {
	// In polyglot compilation, I64 is used as a generic placeholder type for
	// cross-language call results.  Allow I64 to be treated as compatible with
	// any scalar type so that mixed-type binary ops from cross-lang calls pass.
	auto is_generic = [](const IRType &t) { return t.kind == IRTypeKind::kI64 || t.kind == IRTypeKind::kInvalid; };

	const auto require_same_scalar = [&]() -> bool {
		if (!((lhs.IsScalar() || is_generic(lhs)) && (rhs.IsScalar() || is_generic(rhs))))
			return Fail("binary operands must be scalar", msg);
		if (!lhs.SameShape(rhs) && !is_generic(lhs) && !is_generic(rhs))
			return Fail("binary operands type mismatch", msg);
		return true;
	};
	const auto require_int = [&]() -> bool {
		if ((!lhs.IsInteger() && !is_generic(lhs)) || (!rhs.IsInteger() && !is_generic(rhs)))
			return Fail("binary operands must be integer", msg);
		if (!lhs.SameShape(rhs) && !is_generic(lhs) && !is_generic(rhs))
			return Fail("binary operands type mismatch", msg);
		return true;
	};
	const auto require_float = [&]() -> bool {
		if ((!lhs.IsFloat() && !is_generic(lhs)) || (!rhs.IsFloat() && !is_generic(rhs)))
			return Fail("binary operands must be float", msg);
		if (!lhs.SameShape(rhs) && !is_generic(lhs) && !is_generic(rhs))
			return Fail("binary operands type mismatch", msg);
		return true;
	};

	switch (bin.op) {
		case BinaryInstruction::Op::kAdd:
		case BinaryInstruction::Op::kSub:
		case BinaryInstruction::Op::kMul:
			if (!require_same_scalar()) return false;
			if (!bin.type.SameShape(lhs) && !is_generic(lhs) && !is_generic(bin.type))
				return Fail("binary result type mismatch", msg);
			return true;
		case BinaryInstruction::Op::kDiv:
		case BinaryInstruction::Op::kSDiv:
		case BinaryInstruction::Op::kUDiv:
		case BinaryInstruction::Op::kRem:
		case BinaryInstruction::Op::kSRem:
		case BinaryInstruction::Op::kURem:
			if (!require_int()) return false;
			if (!bin.type.SameShape(lhs) && !is_generic(lhs) && !is_generic(bin.type))
				return Fail("binary result type mismatch", msg);
			return true;
		case BinaryInstruction::Op::kAnd:
		case BinaryInstruction::Op::kOr:
		case BinaryInstruction::Op::kXor:
		case BinaryInstruction::Op::kShl:
		case BinaryInstruction::Op::kLShr:
		case BinaryInstruction::Op::kAShr:
			if (!require_int()) return false;
			if (!bin.type.SameShape(lhs) && !is_generic(lhs) && !is_generic(bin.type))
				return Fail("binary result type mismatch", msg);
			return true;
		case BinaryInstruction::Op::kCmpEq:
		case BinaryInstruction::Op::kCmpNe:
			if (!require_same_scalar()) return false;
			if (bin.type.kind != IRTypeKind::kI1) return Fail("cmp result must be i1", msg);
			return true;
		case BinaryInstruction::Op::kCmpUlt:
		case BinaryInstruction::Op::kCmpUle:
		case BinaryInstruction::Op::kCmpUgt:
		case BinaryInstruction::Op::kCmpUge:
			if (!require_int()) return false;
			if (bin.type.kind != IRTypeKind::kI1) return Fail("cmp result must be i1", msg);
			return true;
		case BinaryInstruction::Op::kCmpSlt:
		case BinaryInstruction::Op::kCmpSle:
		case BinaryInstruction::Op::kCmpSgt:
		case BinaryInstruction::Op::kCmpSge:
		case BinaryInstruction::Op::kCmpLt:
			if (!require_int()) return false;
			if (bin.type.kind != IRTypeKind::kI1) return Fail("cmp result must be i1", msg);
			return true;
		case BinaryInstruction::Op::kCmpFoe:
		case BinaryInstruction::Op::kCmpFne:
		case BinaryInstruction::Op::kCmpFlt:
		case BinaryInstruction::Op::kCmpFle:
		case BinaryInstruction::Op::kCmpFgt:
		case BinaryInstruction::Op::kCmpFge:
			if (!require_float()) return false;
			if (bin.type.kind != IRTypeKind::kI1) return Fail("cmp result must be i1", msg);
			return true;
		case BinaryInstruction::Op::kFAdd:
		case BinaryInstruction::Op::kFSub:
		case BinaryInstruction::Op::kFMul:
		case BinaryInstruction::Op::kFDiv:
		case BinaryInstruction::Op::kFRem:
			if (!require_float()) return false;
			if (!bin.type.SameShape(lhs) && !is_generic(lhs) && !is_generic(bin.type))
				return Fail("binary result type mismatch", msg);
			return true;
	}
	return Fail("unknown binary op", msg);
}

bool CheckCast(const CastInstruction &cast, const IRType &src, std::string *msg) {
	const IRType &dst = cast.type;
	switch (cast.cast) {
		case CastInstruction::CastKind::kZExt:
		case CastInstruction::CastKind::kSExt:
			if (!src.IsInteger() || !dst.IsInteger()) return Fail("zext/sext require integer types", msg);
			if (!src.CanLosslesslyConvertTo(dst)) return Fail("zext/sext must widen", msg);
			return true;
		case CastInstruction::CastKind::kTrunc:
			if (!src.IsInteger() || !dst.IsInteger()) return Fail("trunc requires integer types", msg);
			if (!dst.CanLosslesslyConvertTo(src)) return Fail("trunc must narrow", msg);
			return true;
		case CastInstruction::CastKind::kBitcast:
			if (!src.CanBitcastTo(dst)) return Fail("illegal bitcast", msg);
			return true;
		case CastInstruction::CastKind::kFpExt:
			if (!src.IsFloat() || !dst.IsFloat()) return Fail("fpext requires float types", msg);
			if (!src.CanLosslesslyConvertTo(dst)) return Fail("fpext must widen", msg);
			return true;
		case CastInstruction::CastKind::kFpTrunc:
			if (!src.IsFloat() || !dst.IsFloat()) return Fail("fptrunc requires float types", msg);
			if (!dst.CanLosslesslyConvertTo(src)) return Fail("fptrunc must narrow", msg);
			return true;
		case CastInstruction::CastKind::kIntToPtr:
			if (!src.IsInteger()) return Fail("inttoptr requires integer source", msg);
			if (dst.kind != IRTypeKind::kPointer && dst.kind != IRTypeKind::kReference) return Fail("inttoptr requires pointer dest", msg);
			return true;
		case CastInstruction::CastKind::kPtrToInt:
			if (src.kind != IRTypeKind::kPointer && src.kind != IRTypeKind::kReference) return Fail("ptrtoint requires pointer source", msg);
			if (!dst.IsInteger()) return Fail("ptrtoint requires integer dest", msg);
			return true;
	}
	return Fail("unknown cast kind", msg);
}

bool CheckGEP(const GetElementPtrInstruction &gep, const IRType &base_ptr, const DataLayout *layout, std::string *msg) {
	if (!(base_ptr.kind == IRTypeKind::kPointer || base_ptr.kind == IRTypeKind::kReference)) {
		return Fail("gep base is not a pointer", msg);
	}
	if (base_ptr.subtypes.empty()) return Fail("gep base missing pointee", msg);

	IRType cur = base_ptr;
	size_t offset = 0;
	size_t cur_align = 1;
	for (size_t idx : gep.indices) {
		switch (cur.kind) {
			case IRTypeKind::kPointer:
			case IRTypeKind::kReference:
				if (cur.subtypes.empty()) return Fail("gep on pointer with unknown pointee", msg);
				cur = cur.subtypes[0];
				if (layout) {
					cur_align = layout->AlignOf(cur);
				}
				break;
			case IRTypeKind::kArray:
			case IRTypeKind::kVector:
				if (cur.count == 0) return Fail("gep on zero-length aggregate", msg);
				if (idx >= cur.count) return Fail("gep index out of bounds", msg);
				if (cur.subtypes.empty()) return Fail("gep on array/vector with unknown element", msg);
				if (layout) {
					size_t elem_size = layout->SizeOf(cur.subtypes[0]);
					size_t elem_align = layout->AlignOf(cur.subtypes[0]);
					if (elem_size == 0) return Fail("gep element has zero size", msg);
					offset += idx * elem_size;
					cur_align = std::max(cur_align, elem_align);
				}
				cur = cur.subtypes[0];
				break;
			case IRTypeKind::kStruct:
				if (idx >= cur.subtypes.size()) return Fail("gep struct field out of bounds", msg);
				if (layout) {
					size_t field_off = 0;
					size_t struct_align = 1;
					for (size_t i = 0; i < cur.subtypes.size(); ++i) {
						size_t a = layout->AlignOf(cur.subtypes[i]);
						size_t s = layout->SizeOf(cur.subtypes[i]);
						struct_align = std::max(struct_align, a);
						field_off = (field_off + (a - 1)) / a * a; // align up
						if (i == idx) {
							offset += field_off;
							cur_align = std::max(cur_align, a);
							break;
						}
						field_off += s;
					}
				}
				cur = cur.subtypes[idx];
				break;
			default:
				return Fail("gep on non-aggregate", msg);
		}
	}

	IRType expect = IRType::Pointer(cur);
	if (!expect.SameShape(gep.type)) return Fail("gep result type mismatch", msg);

	// If layout exists, ensure we can compute size/align of the final pointee (not used yet for offset, but rejects unsized types).
	if (layout) {
		size_t align = layout->AlignOf(cur);
		size_t size = layout->SizeOf(cur);
		if (size == 0 && cur.kind != IRTypeKind::kFunction && cur.kind != IRTypeKind::kVoid && cur.kind != IRTypeKind::kInvalid) {
			return Fail("gep result has unsized type", msg);
		}
		if (align == 0) {
			return Fail("gep result has invalid alignment", msg);
		}
		( void )offset;  // offset computed for validation; could be used later
	}

	return true;
}
}  // namespace

bool Verify(const Function &func, std::string *msg) {
	return Verify(func, nullptr, msg);
}

// Internal implementation that accepts optional external definitions (globals, function names).
static bool VerifyImpl(const Function &func, const DataLayout *layout,
                       const std::unordered_set<std::string> *extra_defs, std::string *msg) {
	// Skip verification for functions with no blocks — these are either
	// forward declarations or external function stubs that will be resolved
	// at link time. Only verify functions that have a body.
	if (func.blocks.empty()) return true;
	if (!func.entry) return Fail("function has no entry block", msg);
	if (func.ret_type.kind == IRTypeKind::kInvalid) return Fail("function missing return type", msg);

	std::unordered_set<const BasicBlock *> block_set;
	std::unordered_set<std::string> block_names;
	for (auto &bb_ptr : func.blocks) {
		block_set.insert(bb_ptr.get());
		if (!block_names.insert(bb_ptr->name).second) return Fail("duplicate block name: " + bb_ptr->name, msg);
	}
	if (block_set.count(func.entry) == 0) return Fail("entry block not in function blocks", msg);

	std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>> succs;
	std::unordered_map<const BasicBlock *, std::vector<const BasicBlock *>> preds;
	auto add_edge = [&](const BasicBlock *from, const BasicBlock *to) -> bool {
		if (!to) return Fail("edge targets null block", msg);
		if (block_set.count(to) == 0) return Fail("terminator targets block outside function", msg);
		succs[from].push_back(to);
		preds[to].push_back(from);
		return true;
	};

	for (auto &bb_ptr : func.blocks) {
		auto *bb = bb_ptr.get();
		if (!bb->terminator) return Fail("block missing terminator: " + bb->name, msg);
		auto *term = bb->terminator.get();
		if (auto *br = dynamic_cast<BranchStatement *>(term)) {
			if (!br->target) return Fail("branch missing target in block " + bb->name, msg);
			if (!add_edge(bb, br->target)) return false;
		} else if (auto *cbr = dynamic_cast<CondBranchStatement *>(term)) {
			if (!cbr->true_target || !cbr->false_target) return Fail("condbr missing target in block " + bb->name, msg);
			if (!add_edge(bb, cbr->true_target)) return false;
			if (!add_edge(bb, cbr->false_target)) return false;
		} else if (auto *sw = dynamic_cast<SwitchStatement *>(term)) {
			for (auto &c : sw->cases) {
				if (!c.target) return Fail("switch case missing target in block " + bb->name, msg);
				if (!add_edge(bb, c.target)) return false;
			}
			if (!sw->default_target) return Fail("switch missing default target in block " + bb->name, msg);
			if (!add_edge(bb, sw->default_target)) return false;
		} else if (dynamic_cast<UnreachableStatement *>(term)) {
			// no successors
		} else if (dynamic_cast<ReturnStatement *>(term)) {
			// no successors
		} else {
			return Fail("unknown terminator in block " + bb->name, msg);
		}
	}

	// Reachability
	std::unordered_set<const BasicBlock *> reachable;
	std::queue<const BasicBlock *> q;
	q.push(func.entry);
	reachable.insert(func.entry);
	while (!q.empty()) {
		auto *b = q.front();
		q.pop();
		auto it = succs.find(b);
		if (it == succs.end()) continue;
		for (auto *succ : it->second) {
			if (reachable.insert(succ).second) q.push(succ);
		}
	}
	// Unreachable blocks are allowed but skipped in dominance checks; still validated for typing/structure below.
	std::vector<const BasicBlock *> reachable_list(reachable.begin(), reachable.end());

	// Compute immediate dominators for reachable blocks.
	std::unordered_map<const BasicBlock *, int> rpo_index;
	{
		std::vector<const BasicBlock *> rpo;
		std::unordered_set<const BasicBlock *> visited;
		std::vector<const BasicBlock *> stack{func.entry};
		while (!stack.empty()) {
			const BasicBlock *n = stack.back();
			stack.pop_back();
			if (visited.count(n)) continue;
			visited.insert(n);
			rpo.push_back(n);
			auto it = succs.find(n);
			if (it != succs.end()) {
				for (auto *succ : it->second) {
					if (reachable.count(succ) && !visited.count(succ)) stack.push_back(succ);
				}
			}
		}
		std::reverse(rpo.begin(), rpo.end());
		for (size_t i = 0; i < rpo.size(); ++i) rpo_index[rpo[i]] = static_cast<int>(i);
	}

	auto Intersect = [&](const BasicBlock *b1, const BasicBlock *b2, const std::unordered_map<const BasicBlock *, const BasicBlock *> &idom) {
		const BasicBlock *i1 = b1;
		const BasicBlock *i2 = b2;
		while (i1 != i2) {
			while (rpo_index.at(i1) < rpo_index.at(i2)) i1 = idom.at(i1);
			while (rpo_index.at(i2) < rpo_index.at(i1)) i2 = idom.at(i2);
		}
		return i1;
	};

	std::unordered_map<const BasicBlock *, const BasicBlock *> idom;
	if (!reachable.empty()) {
		idom[func.entry] = func.entry;
		bool changed = true;
		while (changed) {
			changed = false;
			for (auto *b : reachable) {
				if (b == func.entry) continue;
				const BasicBlock *new_idom = nullptr;
				auto pred_it = preds.find(b);
				if (pred_it == preds.end()) continue;
				for (auto *p : pred_it->second) {
					if (!reachable.count(p)) continue;
					auto id_it = idom.find(p);
					if (id_it == idom.end()) continue;
					if (!new_idom) {
						new_idom = p;
					} else {
						new_idom = Intersect(p, new_idom, idom);
					}
				}
				if (!new_idom) continue;
				if (idom[b] != new_idom) {
					idom[b] = new_idom;
					changed = true;
				}
			}
		}
	}

	auto Dominates = [&](const BasicBlock *a, const BasicBlock *b) {
		if (a == b) return true;
		auto it = idom.find(b);
		if (it == idom.end()) return false;
		const BasicBlock *runner = it->second;
		while (runner && runner != a) {
			auto it2 = idom.find(runner);
			if (it2 == idom.end()) return false;
			if (it2->second == runner) break;  // reached entry without finding a
			runner = it2->second;
		}
		return runner == a;
	};

	// Collect definitions (params are treated as pre-defined names with unknown type)
	std::unordered_set<std::string> defs(func.params.begin(), func.params.end());
	std::unordered_map<std::string, IRType> types;
	for (size_t i = 0; i < func.params.size(); ++i) {
		IRType param_ty = (i < func.param_types.size()) ? func.param_types[i] : IRType::Invalid();
		types[func.params[i]] = param_ty;
	}
	struct DefLoc {
		const BasicBlock *block;
		int index;  // order within block, params use -1
	};
	std::unordered_map<std::string, DefLoc> def_locs;
	for (const auto &p : func.params) {
		def_locs[p] = {nullptr, -1};
	}

	// Pass 1: Collect ALL definitions (phis and instructions) from ALL blocks
	for (auto &bb_ptr : func.blocks) {
		auto *bb = bb_ptr.get();
		int order = 0;

		for (auto &phi : bb->phis) {
			if (phi->HasResult()) {
				if (!defs.insert(phi->name).second) return Fail("duplicate SSA name: " + phi->name, msg);
				types[phi->name] = phi->type;
				def_locs[phi->name] = {bb, order++};
			}
		}

		for (auto &inst : bb->instructions) {
			if (inst->HasResult()) {
				if (!defs.insert(inst->name).second) return Fail("duplicate SSA name: " + inst->name, msg);
				types[inst->name] = inst->type;
				def_locs[inst->name] = {bb, order++};
			}
		}
	}

	// Pass 2: Validate structure, operand uses, and phi constraints
	for (auto &bb_ptr : func.blocks) {
		auto *bb = bb_ptr.get();

		bool seen_terminator = false;
		for (auto &inst : bb->instructions) {
			if (inst->IsTerminator()) return Fail("terminator found inside instruction list in block " + bb->name, msg);
			if (seen_terminator) return Fail("instruction appears after terminator in block " + bb->name, msg);
		}
		seen_terminator = bb->terminator != nullptr;

		// operand checks for instructions and terminator
		auto check_dom_use = [&](const std::string &op, const BasicBlock *use_block, int use_index, const BasicBlock *incoming_pred) -> bool {
			auto it = def_locs.find(op);
			if (it == def_locs.end()) return true;  // undefined handled elsewhere
			const DefLoc &def = it->second;
			if (def.block == nullptr) return true;  // params dominate all
			if (!reachable.count(use_block)) return true;  // skip dominance for unreachable uses
			if (!reachable.count(def.block)) return true;   // def unreachable; skip dominance
			if (incoming_pred) {
				const BasicBlock *pred = incoming_pred;
				if (!reachable.count(pred)) return true;
				if (def.block == pred) return true;
				if (!Dominates(def.block, pred)) return Fail("phi incoming not dominated by definition: " + op, msg);
				return true;
			}
			if (def.block == use_block) {
				if (def.index >= use_index) return Fail("use before def in block " + use_block->name + " for value " + op, msg);
				return true;
			}
			if (!Dominates(def.block, use_block)) return Fail("use not dominated by definition in block " + use_block->name + " for value " + op, msg);
			return true;
		};

		auto check_operands = [&](const Instruction &inst, int use_index) -> bool {
			for (const auto &op : inst.operands) {
				if (!IsDefined(op, defs, extra_defs)) return Fail("use of undefined value: " + op + " in block " + bb->name, msg);
				if (!check_dom_use(op, bb, use_index, nullptr)) return false;
			}
			return true;
		};

		const auto pred_it = preds.find(bb);
		std::vector<const BasicBlock *> empty_preds;
		const auto &pred_list = (pred_it == preds.end()) ? empty_preds : pred_it->second;
		// phi incoming must match predecessors (count and order)
		for (auto &phi : bb->phis) {
			if (phi->incomings.size() != pred_list.size()) {
				return Fail("phi incoming count mismatch predecessors in block " + bb->name, msg);
			}
			for (size_t i = 0; i < phi->incomings.size(); ++i) {
				auto &inc = phi->incomings[i];
				if (!inc.first) return Fail("phi missing predecessor in block " + bb->name, msg);
				if (i >= pred_list.size() || inc.first != pred_list[i]) {
					return Fail("phi predecessor order mismatch in block " + bb->name, msg);
				}
				if (!IsDefined(inc.second, defs, extra_defs)) {
					return Fail("phi uses undefined value " + inc.second + " in block " + bb->name, msg);
				}
				if (!check_dom_use(inc.second, bb, 0, inc.first)) return false;
				if (phi->HasResult()) {
					IRType inc_ty = LookupType(inc.second, types);
					// Skip type check for:
					//  - "undef" incomings (from paths where the value was never defined)
					//  - Invalid types (phi or incoming type is unknown/unset)
					bool phi_type_ok = inc.second == "undef"
					                || inc_ty.SameShape(phi->type)
					                || inc_ty.kind == IRTypeKind::kInvalid
					                || phi->type.kind == IRTypeKind::kInvalid;
					if (!phi_type_ok) {
						return Fail("phi incoming type mismatch in block " + bb->name, msg);
					}
				}
			}
		}

		int inst_use_index = static_cast<int>(bb->phis.size());
		for (auto &inst : bb->instructions) {
			if (!check_operands(*inst, inst_use_index)) return false;

			if (auto *bin = dynamic_cast<BinaryInstruction *>(inst.get())) {
				if (bin->operands.size() < 2) return Fail("binary missing operands in block " + bb->name, msg);
				IRType lhs = LookupType(bin->operands[0], types);
				IRType rhs = LookupType(bin->operands[1], types);
				if (!CheckBinary(*bin, lhs, rhs, msg)) return false;
			}

			if (auto *ld = dynamic_cast<LoadInstruction *>(inst.get())) {
				if (ld->operands.size() < 1) return Fail("load missing address in block " + bb->name, msg);
				IRType ptr_ty = LookupType(ld->operands[0], types);
				if (!(ptr_ty.kind == IRTypeKind::kPointer || ptr_ty.kind == IRTypeKind::kReference)) {
					return Fail("load address is not a pointer in block " + bb->name, msg);
				}
				if (ptr_ty.subtypes.empty()) return Fail("load pointer missing pointee type in block " + bb->name, msg);
				// Allow type mismatch when load type or pointee is I64 (generic)
				bool ld_compatible = ld->type.SameShape(ptr_ty.subtypes[0])
				                  || ld->type.kind == IRTypeKind::kI64
				                  || ptr_ty.subtypes[0].kind == IRTypeKind::kI64
				                  || ld->type.kind == IRTypeKind::kInvalid;
				if (!ld_compatible) return Fail("load type mismatch pointee in block " + bb->name, msg);
				size_t natural = NaturalAlign(ptr_ty.subtypes[0], layout);
				size_t requested = ld->align ? ld->align : natural;
				if (!IsPowerOfTwo(requested)) return Fail("load alignment not power of two in block " + bb->name, msg);
				if (requested < natural) return Fail("load alignment smaller than natural in block " + bb->name, msg);
			}

			if (auto *st = dynamic_cast<StoreInstruction *>(inst.get())) {
				if (st->operands.size() < 2) return Fail("store missing operands in block " + bb->name, msg);
				IRType ptr_ty = LookupType(st->operands[0], types);
				IRType val_ty = LookupType(st->operands[1], types);
				if (!(ptr_ty.kind == IRTypeKind::kPointer || ptr_ty.kind == IRTypeKind::kReference)) {
					return Fail("store address is not a pointer in block " + bb->name, msg);
				}
				if (ptr_ty.subtypes.empty()) return Fail("store pointer missing pointee type in block " + bb->name, msg);
				// Allow type mismatch when val_ty is Invalid (e.g., literal
				// constants or cross-language values whose type is not tracked)
				// or when val_ty is I64 storing to a scalar pointee (generic).
				bool st_compatible = ptr_ty.subtypes[0].SameShape(val_ty)
				                  || val_ty.kind == IRTypeKind::kInvalid
				                  || (val_ty.kind == IRTypeKind::kI64 && ptr_ty.subtypes[0].IsScalar())
				                  || (ptr_ty.subtypes[0].kind == IRTypeKind::kI64 && val_ty.IsScalar());
				if (!st_compatible) return Fail("store value type mismatch pointee in block " + bb->name, msg);
				size_t natural = NaturalAlign(ptr_ty.subtypes[0], layout);
				size_t requested = st->align ? st->align : natural;
				if (!IsPowerOfTwo(requested)) return Fail("store alignment not power of two in block " + bb->name, msg);
				if (requested < natural) return Fail("store alignment smaller than natural in block " + bb->name, msg);
			}

			if (auto *cast = dynamic_cast<CastInstruction *>(inst.get())) {
				if (cast->operands.size() < 1) return Fail("cast missing operand in block " + bb->name, msg);
				IRType src_ty = LookupType(cast->operands[0], types);
				if (!CheckCast(*cast, src_ty, msg)) return false;
			}

			if (auto *mc = dynamic_cast<MemcpyInstruction *>(inst.get())) {
				if (mc->operands.size() < 3) return Fail("memcpy missing operands in block " + bb->name, msg);
				IRType dst_ty = LookupType(mc->operands[0], types);
				IRType src_ty = LookupType(mc->operands[1], types);
				IRType size_ty = LookupType(mc->operands[2], types);
				if (!(dst_ty.kind == IRTypeKind::kPointer || dst_ty.kind == IRTypeKind::kReference)) return Fail("memcpy dst not pointer", msg);
				if (!(src_ty.kind == IRTypeKind::kPointer || src_ty.kind == IRTypeKind::kReference)) return Fail("memcpy src not pointer", msg);
				if (!size_ty.IsInteger()) return Fail("memcpy size not integer", msg);
				if (!dst_ty.subtypes.empty() && !src_ty.subtypes.empty() && !dst_ty.subtypes[0].SameShape(src_ty.subtypes[0])) {
					return Fail("memcpy src/dst pointee mismatch", msg);
				}
				size_t natural = dst_ty.subtypes.empty() ? 1 : NaturalAlign(dst_ty.subtypes[0], layout);
				size_t requested = mc->align ? mc->align : natural;
				if (!IsPowerOfTwo(requested)) return Fail("memcpy alignment not power of two in block " + bb->name, msg);
				if (requested < natural) return Fail("memcpy alignment smaller than natural in block " + bb->name, msg);
			}

			if (auto *ms = dynamic_cast<MemsetInstruction *>(inst.get())) {
				if (ms->operands.size() < 3) return Fail("memset missing operands in block " + bb->name, msg);
				IRType dst_ty = LookupType(ms->operands[0], types);
				IRType val_ty = LookupType(ms->operands[1], types);
				IRType size_ty = LookupType(ms->operands[2], types);
				if (!(dst_ty.kind == IRTypeKind::kPointer || dst_ty.kind == IRTypeKind::kReference)) return Fail("memset dst not pointer", msg);
				if (!val_ty.IsInteger()) return Fail("memset value not integer", msg);
				if (!size_ty.IsInteger()) return Fail("memset size not integer", msg);
				size_t natural = dst_ty.subtypes.empty() ? 1 : NaturalAlign(dst_ty.subtypes[0], layout);
				size_t requested = ms->align ? ms->align : natural;
				if (!IsPowerOfTwo(requested)) return Fail("memset alignment not power of two in block " + bb->name, msg);
				if (requested < natural) return Fail("memset alignment smaller than natural in block " + bb->name, msg);
			}

			if (auto *gep = dynamic_cast<GetElementPtrInstruction *>(inst.get())) {
				if (gep->operands.size() < 1) return Fail("gep missing base in block " + bb->name, msg);
				IRType base_ptr = LookupType(gep->operands[0], types);
				if (!(base_ptr.kind == IRTypeKind::kPointer || base_ptr.kind == IRTypeKind::kReference)) {
					return Fail("gep base is not a pointer in block " + bb->name, msg);
				}
				IRType expect_source = base_ptr.subtypes.empty() ? IRType::Invalid() : base_ptr.subtypes[0];
				if (!expect_source.SameShape(gep->source_type)) {
					return Fail("gep source_type mismatch base pointee in block " + bb->name, msg);
				}
				if (!CheckGEP(*gep, base_ptr, layout, msg)) return false;
			}

			if (auto *call = dynamic_cast<CallInstruction *>(inst.get())) {
				bool skip_call_check = false;
				IRType fn_ty = call->callee_type;
				if (fn_ty.kind == IRTypeKind::kInvalid) {
					IRType callee_ty = LookupType(call->callee, types);
					fn_ty = callee_ty;
					if (fn_ty.kind == IRTypeKind::kPointer || fn_ty.kind == IRTypeKind::kReference) {
						if (!fn_ty.subtypes.empty()) fn_ty = fn_ty.subtypes[0];
					}
					if (fn_ty.kind == IRTypeKind::kInvalid) {
						// The callee's type is not available in local scope.
						// Skip type validation for:
						//  - cross-language bridge stubs (__ploy_*, polyglot_*)
						//  - any callee whose type is not locally known (inter-module
						//    or external calls resolved at link time)
						skip_call_check = true;
					}
				}
				if (!skip_call_check) {
					if (fn_ty.kind != IRTypeKind::kFunction) return Fail("call callee is not a function", msg);
					const size_t param_count = fn_ty.count;
					if (!call->is_vararg && param_count != call->operands.size()) return Fail("call arg count mismatch", msg);
					if (call->is_vararg && call->operands.size() < param_count) return Fail("call vararg missing fixed args", msg);
					if (!call->type.SameShape(fn_ty.subtypes[0])) return Fail("call return type mismatch", msg);
					for (size_t i = 0; i < std::min(call->operands.size(), fn_ty.subtypes.size() - 1); ++i) {
						IRType arg_ty = LookupType(call->operands[i], types);
						if (!arg_ty.SameShape(fn_ty.subtypes[i + 1])) return Fail("call arg type mismatch", msg);
					}
				}
			}
			++inst_use_index;
		}

		if (bb->terminator) {
			if (!check_operands(*bb->terminator, inst_use_index)) return false;
			if (auto *ret = dynamic_cast<ReturnStatement *>(bb->terminator.get())) {
				const IRType fn_ret = func.ret_type;
				if (fn_ret.kind == IRTypeKind::kVoid) {
					if (!ret->operands.empty()) return Fail("return with value in void function in block " + bb->name, msg);
				} else {
					if (ret->operands.size() != 1) return Fail("return missing value in non-void function in block " + bb->name, msg);
					IRType val_ty = LookupType(ret->operands[0], types);
					// In polyglot compilation, cross-language stubs often use I64 as
					// a generic return type.  Allow I64 to be returned from functions
					// that expect a pointer/reference (same size on 64-bit targets).
					bool compatible = val_ty.SameShape(fn_ret);
					if (!compatible && val_ty.kind == IRTypeKind::kI64) {
						compatible = fn_ret.kind == IRTypeKind::kPointer ||
						             fn_ret.kind == IRTypeKind::kReference ||
						             fn_ret.kind == IRTypeKind::kF64 ||
						             fn_ret.IsInteger();
					}
					if (!compatible && fn_ret.kind == IRTypeKind::kI64) {
						compatible = val_ty.kind == IRTypeKind::kPointer ||
						             val_ty.kind == IRTypeKind::kReference ||
						             val_ty.kind == IRTypeKind::kF64 ||
						             val_ty.IsInteger();
					}
					if (!compatible && val_ty.kind == IRTypeKind::kInvalid) {
						compatible = true;  // unknown type from external call
					}
					if (!compatible) return Fail("return value type mismatch in block " + bb->name, msg);
				}
			}

			if (auto *br = dynamic_cast<BranchStatement *>(bb->terminator.get())) {
				if (!br->target) return Fail("branch missing target in block " + bb->name, msg);
			}

			if (auto *cbr = dynamic_cast<CondBranchStatement *>(bb->terminator.get())) {
				if (cbr->operands.size() < 1) return Fail("condbr missing condition in block " + bb->name, msg);
				IRType cond_ty = LookupType(cbr->operands[0], types);
				// Allow I64 as a branch condition in polyglot IR — cross-language
				// calls often return I64 as a generic boolean-like value.
				if (cond_ty.kind != IRTypeKind::kI1 && !cond_ty.IsInteger() &&
				    cond_ty.kind != IRTypeKind::kInvalid)
					return Fail("condbr condition must be i1 in block " + bb->name, msg);
				if (!cbr->true_target || !cbr->false_target) return Fail("condbr missing target in block " + bb->name, msg);
			}

			if (auto *sw = dynamic_cast<SwitchStatement *>(bb->terminator.get())) {
				if (sw->operands.size() < 1) return Fail("switch missing operand in block " + bb->name, msg);
				IRType scrut_ty = LookupType(sw->operands[0], types);
				if (!scrut_ty.IsInteger()) return Fail("switch operand must be integer in block " + bb->name, msg);
				if (!sw->default_target) return Fail("switch missing default target in block " + bb->name, msg);
				std::unordered_set<long long> seen_cases;
				for (auto &c : sw->cases) {
					if (!c.target) return Fail("switch case missing target in block " + bb->name, msg);
					if (!seen_cases.insert(c.value).second) return Fail("duplicate switch case value in block " + bb->name, msg);
				}
			}
		}
	}

	return true;
}

bool Verify(const Function &func, const DataLayout *layout, std::string *msg) {
	return VerifyImpl(func, layout, nullptr, msg);
}

bool Verify(const IRContext &ctx, std::string *msg) {
	// Collect global variable and function names as externally-defined symbols
	// that may be referenced as operands inside function bodies.
	std::unordered_set<std::string> external_defs;
	for (auto &gv : ctx.Globals()) {
		if (gv && !gv->name.empty()) external_defs.insert(gv->name);
	}
	for (auto &fn : ctx.Functions()) {
		if (fn && !fn->name.empty()) external_defs.insert(fn->name);
	}
	for (auto &fn : ctx.Functions()) {
		if (!VerifyImpl(*fn, &ctx.Layout(), &external_defs, msg)) return false;
	}
	return true;
}

bool Verify(const IRContext &ctx, const VerifyOptions &opts, std::string *msg) {
	// Standard verification first
	if (!Verify(ctx, msg)) return false;

	// In strict mode, reject placeholder patterns that indicate incomplete
	// type information or unresolved cross-language bridges.
	if (opts.strict) {
		for (auto &fn : ctx.Functions()) {
			// Skip bridge stubs — they are pre-compiled and bypass type checks
			if (fn->is_bridge_stub) continue;
			if (fn->blocks.empty()) continue;

			for (auto &bb : fn->blocks) {
				for (auto &inst : bb->instructions) {
					// Reject "undef" operands in strict mode
					for (const auto &op : inst->operands) {
						if (op == "undef") {
							return Fail("strict: placeholder 'undef' operand in function '" +
							            fn->name + "' block '" + bb->name + "'", msg);
						}
					}
				}
			}
		}
	}
	return true;
}

}  // namespace polyglot::ir
