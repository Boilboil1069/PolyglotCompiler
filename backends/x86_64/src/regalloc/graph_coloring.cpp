/**
 * @file     graph_coloring.cpp
 * @brief    x86-64 graph-coloring register allocator consuming the target's
 *           CFG-aware live intervals.
 *
 * @ingroup  Backend / x86-64 / Register Allocation
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/x86_64/include/machine_ir.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

namespace polyglot::backends::x86_64 {

namespace {

bool Overlaps(const LiveInterval& lhs, const LiveInterval& rhs) {
    return !(lhs.end < rhs.start || rhs.end < lhs.start);
}

}  // namespace

AllocationResult GraphColoringAllocate(const MachineFunction&        fn,
                                       const std::vector<Register>&  available) {
    AllocationResult result;
    const auto intervals = ComputeLiveIntervals(fn);
    if (intervals.empty()) {
        return result;
    }

    if (available.empty()) {
        for (const auto& interval : intervals) {
            result.vreg_to_slot[interval.vreg] = result.stack_slots++;
        }
        return result;
    }

    const std::size_t count = intervals.size();
    std::vector<std::vector<std::size_t>> interference(count);
    for (std::size_t lhs = 0; lhs < count; ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < count; ++rhs) {
            if (!Overlaps(intervals[lhs], intervals[rhs])) {
                continue;
            }
            interference[lhs].push_back(rhs);
            interference[rhs].push_back(lhs);
        }
    }

    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        if (interference[lhs].size() == interference[rhs].size()) {
            if (intervals[lhs].start == intervals[rhs].start) {
                return intervals[lhs].vreg < intervals[rhs].vreg;
            }
            return intervals[lhs].start < intervals[rhs].start;
        }
        return interference[lhs].size() > interference[rhs].size();
    });

    for (std::size_t index : order) {
        std::vector<Register> used;
        for (std::size_t neighbor : interference[index]) {
            const auto assigned = result.vreg_to_phys.find(intervals[neighbor].vreg);
            if (assigned != result.vreg_to_phys.end()) {
                used.push_back(assigned->second);
            }
        }

        const auto free = std::find_if(available.begin(), available.end(),
                                       [&](Register candidate) {
                                           return std::find(used.begin(), used.end(), candidate) ==
                                                  used.end();
                                       });
        if (free == available.end()) {
            result.vreg_to_slot[intervals[index].vreg] = result.stack_slots++;
        } else {
            result.vreg_to_phys[intervals[index].vreg] = *free;
        }
    }

    return result;
}

}  // namespace polyglot::backends::x86_64
