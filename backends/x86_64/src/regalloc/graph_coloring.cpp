#include "backends/x86_64/include/machine_ir.h"

#include <algorithm>
#include <numeric>
#include <vector>

namespace polyglot::backends::x86_64 {
namespace {

bool Overlaps(const LiveInterval &a, const LiveInterval &b) {
  return !(a.end <= b.start || b.end <= a.start);
}

}  // namespace

AllocationResult GraphColoringAllocate(const MachineFunction &fn, const std::vector<Register> &available) {
  AllocationResult result;
  auto intervals = ComputeLiveIntervals(fn);
  if (intervals.empty()) return result;

  if (available.empty()) {
    for (const auto &li : intervals) result.vreg_to_slot[li.vreg] = result.stack_slots++;
    return result;
  }

  const std::size_t n = intervals.size();
  std::vector<std::vector<std::size_t>> graph(n);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      if (Overlaps(intervals[i], intervals[j])) {
        graph[i].push_back(j);
        graph[j].push_back(i);
      }
    }
  }

  std::vector<std::size_t> order(n);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
    if (graph[lhs].size() == graph[rhs].size()) return intervals[lhs].start < intervals[rhs].start;
    return graph[lhs].size() > graph[rhs].size();
  });

  for (std::size_t idx : order) {
    std::vector<Register> used_regs;
    for (std::size_t neighbor : graph[idx]) {
      auto phys_it = result.vreg_to_phys.find(intervals[neighbor].vreg);
      if (phys_it != result.vreg_to_phys.end()) used_regs.push_back(phys_it->second);
    }

    Register chosen;
    bool found = false;
    for (auto reg : available) {
      if (std::find(used_regs.begin(), used_regs.end(), reg) == used_regs.end()) {
        chosen = reg;
        found = true;
        break;
      }
    }

    if (found) {
      result.vreg_to_phys[intervals[idx].vreg] = chosen;
    } else {
      result.vreg_to_slot[intervals[idx].vreg] = result.stack_slots++;
    }
  }

  return result;
}

}  // namespace polyglot::backends::x86_64
