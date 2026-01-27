#include "backends/arm64/include/machine_ir.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace polyglot::backends::arm64 {
std::vector<LiveInterval> ComputeLiveIntervals(const MachineFunction &fn) {
  std::unordered_map<int, LiveInterval> intervals;
  int position = 0;
  auto ensure = [&](int vreg) -> LiveInterval & {
    auto it = intervals.find(vreg);
    if (it != intervals.end()) return it->second;
    LiveInterval li;
    li.vreg = vreg;
    li.start = position;
    li.end = position;
    auto [inserted_it, _] = intervals.emplace(vreg, li);
    return inserted_it->second;
  };

  for (auto &bb : fn.blocks) {
    for (auto &mi : bb.instructions) {
      if (mi.def >= 0) {
        auto &li = ensure(mi.def);
        li.start = std::min(li.start, position);
        li.end = std::max(li.end, position);
      }
      for (int use : mi.uses) {
        auto &li = ensure(use);
        li.end = std::max(li.end, position);
      }
      position += 2;
    }
  }

  std::vector<LiveInterval> out;
  out.reserve(intervals.size());
  for (auto &kv : intervals) out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const LiveInterval &a, const LiveInterval &b) {
    return a.start < b.start;
  });
  return out;
}

namespace {

void ExpireOldIntervals(std::vector<LiveInterval> &active, int position,
                        std::vector<Register> &free_regs) {
  auto it = active.begin();
  while (it != active.end()) {
    if (it->end >= position) {
      ++it;
    } else {
      free_regs.push_back(it->phys);
      it = active.erase(it);
    }
  }
  std::sort(active.begin(), active.end(), [](const LiveInterval &a, const LiveInterval &b) {
    return a.end < b.end;
  });
}

}  // namespace

AllocationResult LinearScanAllocate(const MachineFunction &fn, const std::vector<Register> &available) {
  auto intervals = ComputeLiveIntervals(fn);
  AllocationResult result;
  std::vector<LiveInterval> active;
  std::vector<Register> free_regs = available;

  for (auto interval : intervals) {
    bool has_phys = false;
    ExpireOldIntervals(active, interval.start, free_regs);

    if (free_regs.empty()) {
      std::sort(active.begin(), active.end(), [](const LiveInterval &a, const LiveInterval &b) {
        return a.end < b.end;
      });
      if (!active.empty() && active.back().end > interval.end) {
        auto spilled_active = active.back();
        active.pop_back();
        result.vreg_to_slot[spilled_active.vreg] = result.stack_slots++;
        interval.phys = spilled_active.phys;
        has_phys = true;
      } else {
        result.vreg_to_slot[interval.vreg] = result.stack_slots++;
        interval.spilled = true;
      }
    }

    if (!interval.spilled) {
      if (!has_phys) {
        if (!free_regs.empty()) {
          interval.phys = free_regs.back();
          free_regs.pop_back();
          has_phys = true;
        }
      } else {
        free_regs.erase(std::remove(free_regs.begin(), free_regs.end(), interval.phys), free_regs.end());
      }
      result.vreg_to_phys[interval.vreg] = interval.phys;
      active.push_back(interval);
      std::sort(active.begin(), active.end(), [](const LiveInterval &a, const LiveInterval &b) {
        return a.end < b.end;
      });
    }
  }

  return result;
}

}  // namespace polyglot::backends::arm64
