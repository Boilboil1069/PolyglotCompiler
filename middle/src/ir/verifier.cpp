#include "middle/include/ir/verifier.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

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
}  // namespace

bool Verify(const Function &func, std::string *msg) {
	if (!func.entry) return Fail("function has no entry block", msg);

	std::unordered_set<const BasicBlock *> block_set;
	for (auto &bb_ptr : func.blocks) block_set.insert(bb_ptr.get());
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

	// Collect definitions (params are treated as pre-defined names)
	std::unordered_set<std::string> defs(func.params.begin(), func.params.end());

	for (auto &bb_ptr : func.blocks) {
		auto *bb = bb_ptr.get();
		// terminator uniqueness
		int term_count = bb->terminator ? 1 : 0;
		for (auto &inst : bb->instructions) {
			if (inst->IsTerminator()) return Fail("terminator found inside instruction list in block " + bb->name, msg);
			if (inst->HasResult()) defs.insert(inst->name);
		}
		for (auto &phi : bb->phis) {
			if (phi->HasResult()) defs.insert(phi->name);
		}
		if (term_count > 1) return Fail("multiple terminators in block " + bb->name, msg);

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

		// phi incoming must match predecessors
		for (auto &phi : bb->phis) {
			for (auto &inc : phi->incomings) {
				if (!inc.first) return Fail("phi missing predecessor in block " + bb->name, msg);
				if (std::find(bb->predecessors.begin(), bb->predecessors.end(), inc.first) == bb->predecessors.end()) {
					return Fail("phi incoming not from predecessor in block " + bb->name, msg);
				}
				if (!IsDefined(inc.second, defs)) {
					return Fail("phi uses undefined value " + inc.second + " in block " + bb->name, msg);
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
			// minimal type sanity for store
			if (auto *st = dynamic_cast<StoreInstruction *>(inst.get())) {
				if (st->operands.size() < 2) return Fail("store missing operands in block " + bb->name, msg);
			}
		}
		if (bb->terminator) {
			// ensure no other instruction after terminator (structure enforces but check successor list)
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
