#include "middle/include/ir/verifier.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/cfg.h"

namespace polyglot::ir {

namespace {
bool Fail(const std::string &reason, std::string *msg) {
	if (msg) *msg = reason;
	return false;
}

bool IsDefined(const std::string &name, const std::unordered_set<std::string> &defs) {
	if (name.empty()) return true;  // allow empty/literal operands
	return defs.count(name) > 0;
}

IRType LookupType(const std::string &name, const std::unordered_map<std::string, IRType> &types) {
	auto it = types.find(name);
	return it == types.end() ? IRType::Invalid() : it->second;
}

bool CheckBinary(const BinaryInstruction &bin, const IRType &lhs, const IRType &rhs, std::string *msg) {
	const bool is_cmp = bin.op == BinaryInstruction::Op::kCmpEq || bin.op == BinaryInstruction::Op::kCmpLt;
	if (!(lhs.IsScalar() && rhs.IsScalar())) return Fail("binary operands must be scalar", msg);
	if (!lhs.SameShape(rhs)) return Fail("binary operands type mismatch", msg);
	if (is_cmp) {
		if (bin.type.kind != IRTypeKind::kI1) return Fail("cmp result must be i1", msg);
		return true;
	}
	if (!bin.type.SameShape(lhs)) return Fail("binary result type mismatch", msg);
	return true;
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
	}
	return Fail("unknown cast kind", msg);
}

bool CheckGEP(const GetElementPtrInstruction &gep, const IRType &base_ptr, std::string *msg) {
	IRType cur = base_ptr;
	for (size_t idx : gep.indices) {
		switch (cur.kind) {
			case IRTypeKind::kPointer:
			case IRTypeKind::kReference:
				if (cur.subtypes.empty()) return Fail("gep on pointer with unknown pointee", msg);
				cur = cur.subtypes[0];
				break;
			case IRTypeKind::kArray:
			case IRTypeKind::kVector:
				if (idx >= cur.count) return Fail("gep index out of bounds", msg);
				if (cur.subtypes.empty()) return Fail("gep on array/vector with unknown element", msg);
				cur = cur.subtypes[0];
				break;
			case IRTypeKind::kStruct:
				if (idx >= cur.subtypes.size()) return Fail("gep struct field out of bounds", msg);
				cur = cur.subtypes[idx];
				break;
			default:
				return Fail("gep on non-aggregate", msg);
		}
	}
	IRType expect = IRType::Pointer(cur);
	if (!expect.SameShape(gep.type)) return Fail("gep result type mismatch", msg);
	return true;
}
}  // namespace

bool Verify(const Function &func, std::string *msg) {
	if (!func.entry) return Fail("function has no entry block", msg);

	std::unordered_set<const BasicBlock *> block_set;
	std::unordered_set<std::string> block_names;
	for (auto &bb_ptr : func.blocks) {
		block_set.insert(bb_ptr.get());
		if (!block_names.insert(bb_ptr->name).second) return Fail("duplicate block name: " + bb_ptr->name, msg);
	}
	if (block_set.count(func.entry) == 0) return Fail("entry block not in function blocks", msg);

	// Reachability
	std::unordered_set<const BasicBlock *> reachable;
	std::queue<const BasicBlock *> q;
	q.push(func.entry);
	reachable.insert(func.entry);
	while (!q.empty()) {
		auto *b = q.front();
		q.pop();
		for (auto *succ : b->successors) {
			if (reachable.insert(succ).second) q.push(succ);
		}
	}
	for (auto *b : block_set) {
		if (!reachable.count(b)) return Fail("unreachable block: " + b->name, msg);
	}

	// Collect definitions (params are treated as pre-defined names with unknown type)
	std::unordered_set<std::string> defs(func.params.begin(), func.params.end());
	std::unordered_map<std::string, IRType> types;
	for (const auto &p : func.params) types[p] = IRType::Invalid();

	for (auto &bb_ptr : func.blocks) {
		auto *bb = bb_ptr.get();
		// terminator presence and uniqueness
		if (!bb->terminator) return Fail("block missing terminator: " + bb->name, msg);

		for (auto &phi : bb->phis) {
			if (phi->HasResult()) {
				if (!defs.insert(phi->name).second) return Fail("duplicate SSA name: " + phi->name, msg);
				types[phi->name] = phi->type;
			}
		}

		bool seen_terminator = false;
		for (auto &inst : bb->instructions) {
			if (inst->IsTerminator()) return Fail("terminator found inside instruction list in block " + bb->name, msg);
			if (seen_terminator) return Fail("instruction appears after terminator in block " + bb->name, msg);
			if (inst->HasResult()) {
				if (!defs.insert(inst->name).second) return Fail("duplicate SSA name: " + inst->name, msg);
				types[inst->name] = inst->type;
			}
		}
		seen_terminator = bb->terminator != nullptr;

		// predecessor/successor consistency
		for (auto *succ : bb->successors) {
			bool back = false;
			for (auto *pred : succ->predecessors) {
				if (pred == bb) {
					back = true;
					break;
				}
			}
			if (!back) return Fail("successor missing back-edge from predecessor in block " + bb->name, msg);
		}
		for (auto *pred : bb->predecessors) {
			bool fwd = false;
			for (auto *succ : pred->successors) {
				if (succ == bb) {
					fwd = true;
					break;
				}
			}
			if (!fwd) return Fail("predecessor missing forward-edge to block " + bb->name, msg);
		}

		// phi incoming must match predecessors (count and order)
		for (auto &phi : bb->phis) {
			if (phi->incomings.size() != bb->predecessors.size()) {
				return Fail("phi incoming count mismatch predecessors in block " + bb->name, msg);
			}
			for (size_t i = 0; i < phi->incomings.size(); ++i) {
				auto &inc = phi->incomings[i];
				if (!inc.first) return Fail("phi missing predecessor in block " + bb->name, msg);
				if (inc.first != bb->predecessors[i]) {
					return Fail("phi predecessor order mismatch in block " + bb->name, msg);
				}
				if (!IsDefined(inc.second, defs)) {
					return Fail("phi uses undefined value " + inc.second + " in block " + bb->name, msg);
				}
				if (phi->HasResult()) {
					IRType inc_ty = LookupType(inc.second, types);
					if (!inc_ty.SameShape(phi->type)) {
						return Fail("phi incoming type mismatch in block " + bb->name, msg);
					}
				}
			}
		}

		// operand checks for instructions and terminator
		auto check_operands = [&](const Instruction &inst) -> bool {
			for (const auto &op : inst.operands) {
				if (!IsDefined(op, defs)) return Fail("use of undefined value: " + op + " in block " + bb->name, msg);
			}
			return true;
		};

		for (auto &inst : bb->instructions) {
			if (!check_operands(*inst)) return false;

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
				if (!ld->type.SameShape(ptr_ty.subtypes[0])) return Fail("load type mismatch pointee in block " + bb->name, msg);
			}

			if (auto *st = dynamic_cast<StoreInstruction *>(inst.get())) {
				if (st->operands.size() < 2) return Fail("store missing operands in block " + bb->name, msg);
				IRType ptr_ty = LookupType(st->operands[0], types);
				IRType val_ty = LookupType(st->operands[1], types);
				if (!(ptr_ty.kind == IRTypeKind::kPointer || ptr_ty.kind == IRTypeKind::kReference)) {
					return Fail("store address is not a pointer in block " + bb->name, msg);
				}
				if (ptr_ty.subtypes.empty()) return Fail("store pointer missing pointee type in block " + bb->name, msg);
				if (!ptr_ty.subtypes[0].SameShape(val_ty)) return Fail("store value type mismatch pointee in block " + bb->name, msg);
			}

			if (auto *cast = dynamic_cast<CastInstruction *>(inst.get())) {
				if (cast->operands.size() < 1) return Fail("cast missing operand in block " + bb->name, msg);
				IRType src_ty = LookupType(cast->operands[0], types);
				if (!CheckCast(*cast, src_ty, msg)) return false;
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
				if (!CheckGEP(*gep, base_ptr, msg)) return false;
			}

			if (auto *call = dynamic_cast<CallInstruction *>(inst.get())) {
				IRType callee_ty = LookupType(call->callee, types);
				IRType fn_ty = callee_ty;
				if (fn_ty.kind == IRTypeKind::kPointer || fn_ty.kind == IRTypeKind::kReference) {
					if (!fn_ty.subtypes.empty()) fn_ty = fn_ty.subtypes[0];
				}
				if (fn_ty.kind == IRTypeKind::kFunction) {
					if (fn_ty.count != call->operands.size()) return Fail("call arg count mismatch", msg);
					if (!call->type.SameShape(fn_ty.subtypes[0])) return Fail("call return type mismatch", msg);
					for (size_t i = 0; i < call->operands.size(); ++i) {
						IRType arg_ty = LookupType(call->operands[i], types);
						if (!arg_ty.SameShape(fn_ty.subtypes[i + 1])) return Fail("call arg type mismatch", msg);
					}
				}
			}
		}

		if (bb->terminator) {
			if (!check_operands(*bb->terminator)) return false;
		}
	}

	return true;
}

bool Verify(const IRContext &ctx, std::string *msg) {
	for (auto &fn : ctx.Functions()) {
		if (!Verify(*fn, msg)) return false;
	}
	return true;
}

}  // namespace polyglot::ir
