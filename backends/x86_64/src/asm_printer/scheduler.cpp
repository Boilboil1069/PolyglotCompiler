/**
 * @file     scheduler.cpp
 * @brief    x86-64 code generation implementation
 *
 * @ingroup  Backend / x86-64
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "backends/x86_64/include/machine_ir.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace polyglot::backends::x86_64 {
namespace {

void ScheduleBlock(MachineBasicBlock &bb) {
  std::vector<MachineInstr> body;
  std::vector<MachineInstr> terminators;
  for (auto &mi : bb.instructions) {
    if (mi.terminator) {
      terminators.push_back(mi);
    } else {
      body.push_back(mi);
    }
  }
  size_t n = body.size();
  if (n == 0) {
    bb.instructions = terminators;
    return;
  }

  std::vector<std::vector<int>> succ(n);
  std::vector<int> indegree(n, 0);
  std::unordered_map<int, int> last_def;

  for (size_t i = 0; i < n; ++i) {
    for (int use : body[i].uses) {
      auto it = last_def.find(use);
      if (it != last_def.end()) {
        succ[it->second].push_back(static_cast<int>(i));
        indegree[i] += 1;
      }
    }
    if (body[i].def >= 0) last_def[body[i].def] = static_cast<int>(i);
  }

  std::vector<MachineInstr> scheduled;
  std::vector<int> ready;
  for (size_t i = 0; i < n; ++i) {
    if (indegree[i] == 0) ready.push_back(static_cast<int>(i));
  }

  while (!ready.empty()) {
    auto best_it = std::max_element(ready.begin(), ready.end(), [&](int a, int b) {
      if (body[a].latency == body[b].latency) return body[a].cost < body[b].cost;
      return body[a].latency < body[b].latency;
    });
    int idx = *best_it;
    ready.erase(best_it);
    scheduled.push_back(body[idx]);
    for (int s : succ[idx]) {
      indegree[s] -= 1;
      if (indegree[s] == 0) ready.push_back(s);
    }
  }

  scheduled.insert(scheduled.end(), terminators.begin(), terminators.end());
  bb.instructions = std::move(scheduled);
}

}  // namespace

void ScheduleFunction(MachineFunction &fn) {
  for (auto &bb : fn.blocks) ScheduleBlock(bb);
}

}  // namespace polyglot::backends::x86_64
