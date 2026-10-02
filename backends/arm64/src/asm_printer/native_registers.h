// Register allocation for the scalar native emitter. All allocated registers
// are callee saved, so live SSA values survive ordinary calls and recursion.
#pragma once

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::backends::arm64 {
namespace native {
using Names = std::set<std::string>;

inline std::vector<std::string> Uses(const ir::Instruction &instruction) {
  auto uses = instruction.operands;
  if (auto call = dynamic_cast<const ir::CallInstruction *>(&instruction); call && call->is_indirect)
    uses.push_back(call->callee);
  return uses;
}

inline std::vector<const ir::BasicBlock *> Successors(const ir::BasicBlock &block) {
  if (auto branch = dynamic_cast<const ir::BranchStatement *>(block.terminator.get()))
    return {branch->target};
  if (auto branch = dynamic_cast<const ir::CondBranchStatement *>(block.terminator.get()))
    return {branch->true_target, branch->false_target};
  if (auto branch = dynamic_cast<const ir::SwitchStatement *>(block.terminator.get())) {
    std::vector<const ir::BasicBlock *> result{branch->default_target};
    for (const auto &item : branch->cases) result.push_back(item.target);
    return result;
  }
  return {};
}

inline std::unordered_map<std::string, unsigned> AllocateRegisters(
    const ir::Function &function, RegAllocStrategy strategy) {
  if (strategy == RegAllocStrategy::kStack) return {};
  struct Block { Names definitions, uses, live_in, live_out; std::size_t begin{}, end{}; };
  std::map<const ir::BasicBlock *, Block> blocks;
  Names values;
  for (const auto &parameter : function.params) values.insert(parameter);
  for (const auto &block : function.blocks) {
    for (const auto &phi : block->phis) values.insert(phi->name);
    for (const auto &instruction : block->instructions)
      if (instruction->HasResult()) values.insert(instruction->name);
  }
  auto uses = [&](const ir::Instruction &instruction, Names &live) {
    for (const auto &name : Uses(instruction)) if (values.count(name)) live.insert(name);
  };
  std::size_t position = 1;
  for (const auto &block : function.blocks) {
    auto &state = blocks[block.get()];
    state.begin = position++;
    for (const auto &phi : block->phis) state.definitions.insert(phi->name);
    auto visit = [&](const ir::Instruction &instruction) {
      for (const auto &name : Uses(instruction))
        if (values.count(name) && !state.definitions.count(name)) state.uses.insert(name);
      if (instruction.HasResult()) state.definitions.insert(instruction.name);
      ++position;
    };
    for (const auto &instruction : block->instructions) visit(*instruction);
    if (block->terminator) visit(*block->terminator);
    state.end = position++;
  }
  // Phi operands are uses on the predecessor edge, not uses in the successor.
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto it = function.blocks.rbegin(); it != function.blocks.rend(); ++it) {
      auto &state = blocks[it->get()];
      Names out;
      for (auto successor : Successors(**it)) {
        if (!successor) continue;
        Names edge = blocks.at(successor).live_in;
        for (const auto &phi : successor->phis) edge.erase(phi->name);
        for (const auto &phi : successor->phis) {
          for (const auto &[predecessor, value] : phi->incomings)
            if (predecessor == it->get() && values.count(value)) edge.insert(value);
        }
        out.insert(edge.begin(), edge.end());
      }
      Names in = state.uses;
      for (const auto &name : out) if (!state.definitions.count(name)) in.insert(name);
      if (in != state.live_in || out != state.live_out) changed = true;
      state.live_in = std::move(in); state.live_out = std::move(out);
    }
  }
  std::map<std::string, Names> interference;
  struct Interval { std::string name; std::size_t begin, end; };
  std::map<std::string, Interval> intervals;
  for (const auto &name : values)
    intervals.emplace(name, Interval{name, std::numeric_limits<std::size_t>::max(), 0});
  auto touch = [&](const std::string &name, std::size_t at) {
    auto found = intervals.find(name);
    if (found == intervals.end()) return;
    found->second.begin = std::min(found->second.begin, at);
    found->second.end = std::max(found->second.end, at);
  };
  auto definition = [&](const std::string &name, Names &live) {
    for (const auto &other : live) if (name != other) {
      interference[name].insert(other); interference[other].insert(name);
    }
    live.erase(name);
  };
  for (const auto &block : function.blocks) {
    const auto &state = blocks.at(block.get());
    for (const auto &name : state.live_in) touch(name, state.begin);
    for (const auto &name : state.live_out) touch(name, state.end);
    auto at = state.begin;
    for (const auto &phi : block->phis) touch(phi->name, at);
    for (const auto &instruction : block->instructions) {
      ++at;
      for (const auto &name : Uses(*instruction)) touch(name, at);
      if (instruction->HasResult()) touch(instruction->name, at);
    }
    if (block->terminator)
      for (const auto &name : Uses(*block->terminator)) touch(name, ++at);
    Names live = state.live_out;
    if (block->terminator) uses(*block->terminator, live);
    for (auto it = block->instructions.rbegin(); it != block->instructions.rend(); ++it) {
      if ((*it)->HasResult()) definition((*it)->name, live);
      uses(**it, live);
    }
    for (auto it = block->phis.rbegin(); it != block->phis.rend(); ++it) definition((*it)->name, live);
    if (block.get() == function.entry || (!function.entry && block == function.blocks.front())) {
      for (auto it = function.params.rbegin(); it != function.params.rend(); ++it) { definition(*it, live); touch(*it, 0); }
    }
  }
  std::unordered_map<std::string, unsigned> allocated;
  if (strategy == RegAllocStrategy::kGraphColoring) {
    std::vector<std::string> ordered(values.begin(), values.end());
    std::stable_sort(ordered.begin(), ordered.end(), [&](const auto &a, const auto &b) {
      return interference[a].size() > interference[b].size();
    });
    for (const auto &name : ordered) {
      std::set<unsigned> occupied;
      for (const auto &other : interference[name]) {
        const auto found = allocated.find(other);
        if (found != allocated.end()) occupied.insert(found->second);
      }
      for (unsigned reg = 19; reg <= 28; ++reg)
        if (!occupied.count(reg)) { allocated[name] = reg; break; }
    }
  } else {
    std::vector<Interval> ordered;
    for (const auto &[name, interval] : intervals) ordered.push_back(interval);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
      return a.begin < b.begin;
    });
    std::vector<Interval> active;
    for (const auto &interval : ordered) {
      active.erase(std::remove_if(active.begin(), active.end(), [&](const auto &other) {
        return other.end < interval.begin;
      }), active.end());
      std::set<unsigned> occupied;
      for (const auto &other : active) occupied.insert(allocated.at(other.name));
      for (unsigned reg = 19; reg <= 28; ++reg)
        if (!occupied.count(reg)) {
          allocated[interval.name] = reg; active.push_back(interval); break;
        }
    }
  }
  return allocated;
}
} // namespace native
} // namespace polyglot::backends::arm64
