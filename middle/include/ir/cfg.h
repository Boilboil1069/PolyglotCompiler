#pragma once

#include <string>
#include <vector>

namespace polyglot::ir {

struct BasicBlock {
  std::string name;
  std::vector<BasicBlock *> successors{};
  std::vector<BasicBlock *> predecessors{};
};

struct ControlFlowGraph {
  BasicBlock *entry{nullptr};
  std::vector<BasicBlock> blocks{};
};

}  // namespace polyglot::ir
