#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

struct Function;

struct BasicBlock {
  std::string name;
  std::vector<std::shared_ptr<PhiInstruction>> phis;       // Phi nodes (dominated by block)
  std::vector<std::shared_ptr<Instruction>> instructions;  // non-phi, non-terminator
  std::shared_ptr<Instruction> terminator{nullptr};
  std::vector<BasicBlock *> successors{};
  std::vector<BasicBlock *> predecessors{};

  void AddInstruction(const std::shared_ptr<Instruction> &inst);
  void AddPhi(const std::shared_ptr<PhiInstruction> &phi);
  void SetTerminator(const std::shared_ptr<Instruction> &term);
};

struct Function {
  std::string name;
  std::vector<std::shared_ptr<BasicBlock>> blocks{};
  BasicBlock *entry{nullptr};
  std::vector<std::string> params{};

  BasicBlock *CreateBlock(const std::string &block_name);
};

struct ControlFlowGraph {
  BasicBlock *entry{nullptr};
  std::vector<BasicBlock *> blocks{};  // non-owning
};

struct DominatorTree {
  std::unordered_map<BasicBlock *, BasicBlock *> idom;  // immediate dominator
  std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> children;
};

using DominanceFrontier = std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>>;

ControlFlowGraph BuildCFG(Function &func);
DominatorTree ComputeDominators(const ControlFlowGraph &cfg);
DominanceFrontier ComputeDominanceFrontier(const ControlFlowGraph &cfg, const DominatorTree &dom_tree);

}  // namespace polyglot::ir
