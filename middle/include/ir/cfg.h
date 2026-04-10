/**
 * @file     cfg.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/nodes/statements.h"

namespace polyglot::ir {

// Lightweight relocation record for precompiled bridge stubs.
// Mirrors the essential fields of linker-level Relocation without
// introducing a dependency on the linker headers.
/** @brief StubRelocation data structure. */
struct StubRelocation {
  size_t offset{0};
  std::string symbol;
  std::uint32_t type{0};
  std::int64_t addend{0};
  bool is_pc_relative{false};
  std::uint8_t size{4};
};

/** @brief BasicBlock data structure. */
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

/** @brief Function data structure. */
struct Function {
  std::string name;
  std::vector<std::shared_ptr<BasicBlock>> blocks{};
  BasicBlock *entry{nullptr};
  std::vector<std::string> params{};
  std::vector<IRType> param_types{};
  IRType ret_type{IRType::Invalid()};
  IRType function_type{IRType::Invalid()};

  // Bridge stub support: when is_bridge_stub is true, the function body
  // is precompiled machine code rather than IR.  The backend should emit
  // precompiled_code directly and apply precompiled_relocs.
  bool is_external{false};
  bool is_bridge_stub{false};
  std::vector<std::uint8_t> precompiled_code;
  std::vector<StubRelocation> precompiled_relocs;

  BasicBlock *CreateBlock(const std::string &block_name);
};

/** @brief ControlFlowGraph data structure. */
struct ControlFlowGraph {
  BasicBlock *entry{nullptr};
  std::vector<BasicBlock *> blocks{};  // non-owning
};

/** @brief DominatorTree data structure. */
struct DominatorTree {
  std::unordered_map<BasicBlock *, BasicBlock *> idom;  // immediate dominator
  std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> children;
};

using DominanceFrontier = std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>>;

ControlFlowGraph BuildCFG(Function &func);
DominatorTree ComputeDominators(const ControlFlowGraph &cfg);
DominanceFrontier ComputeDominanceFrontier(const ControlFlowGraph &cfg, const DominatorTree &dom_tree);

}  // namespace polyglot::ir
