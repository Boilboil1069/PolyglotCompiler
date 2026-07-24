/**
 * @file     linear_scan.cpp
 * @brief    x86-64 CFG-aware live-interval analysis and linear-scan register
 *           allocation. Successor edges are recovered from machine branch
 *           labels so values live across joins and loop backedges cannot be
 *           overwritten by later blocks in layout order.
 *
 * @ingroup  Backend / x86-64 / Register Allocation
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/x86_64/include/machine_ir.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace polyglot::backends::x86_64 {

namespace {

using VRegSet = std::unordered_set<int>;

struct BlockLiveness {
    int                       start{0};
    int                       end{0};
    VRegSet                   use;
    VRegSet                   def;
    VRegSet                   live_in;
    VRegSet                   live_out;
    std::vector<std::size_t>  successors;
};

void AddSuccessor(const Operand& operand,
                  const std::unordered_map<std::string, std::size_t>& block_for_label,
                  std::vector<std::size_t>& successors) {
    if (operand.kind != Operand::Kind::kLabel) {
        return;
    }
    const auto target = block_for_label.find(operand.label);
    if (target == block_for_label.end()) {
        return;
    }
    if (std::find(successors.begin(), successors.end(), target->second) ==
        successors.end()) {
        successors.push_back(target->second);
    }
}

std::vector<BlockLiveness> AnalyzeBlocks(const MachineFunction& fn) {
    std::vector<BlockLiveness> blocks(fn.blocks.size());
    std::unordered_map<std::string, std::size_t> block_for_label;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        block_for_label.emplace(fn.blocks[i].name, i);
    }

    int position = 0;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        auto& info = blocks[i];
        info.start = position;
        info.end = position;
        for (const auto& instruction : fn.blocks[i].instructions) {
            for (int use : instruction.uses) {
                if (info.def.count(use) == 0) {
                    info.use.insert(use);
                }
            }
            if (instruction.def >= 0) {
                info.def.insert(instruction.def);
            }
            info.end = position;
            position += 2;
        }

        bool has_control_transfer = false;
        bool has_unconditional_transfer = false;
        bool has_return = false;
        std::size_t conditional_label_count = 0;
        for (const auto& instruction : fn.blocks[i].instructions) {
            if (instruction.opcode == Opcode::kRet) {
                has_control_transfer = true;
                has_return = true;
                continue;
            }
            if (instruction.opcode != Opcode::kJmp &&
                instruction.opcode != Opcode::kJcc) {
                continue;
            }

            has_control_transfer = true;
            has_unconditional_transfer |= instruction.opcode == Opcode::kJmp;
            for (const auto& operand : instruction.operands) {
                if (operand.kind == Operand::Kind::kLabel) {
                    if (instruction.opcode == Opcode::kJcc) {
                        ++conditional_label_count;
                    }
                    AddSuccessor(operand, block_for_label, info.successors);
                }
            }
        }

        // Legacy one-target Jcc blocks either end with a separate JMP false
        // edge or fall through to the lexically following block.
        if (conditional_label_count == 1 && !has_unconditional_transfer &&
            !has_return && i + 1 < fn.blocks.size() &&
            std::find(info.successors.begin(), info.successors.end(), i + 1) ==
                info.successors.end()) {
            info.successors.push_back(i + 1);
        }
        if (!has_control_transfer && i + 1 < fn.blocks.size()) {
            info.successors.push_back(i + 1);
        }
    }

    // Standard backwards data-flow equations:
    //   live_out[B] = union(live_in[S]) for every successor S
    //   live_in[B]  = use[B] union (live_out[B] - def[B])
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = blocks.size(); index-- > 0;) {
            auto& info = blocks[index];
            VRegSet new_out;
            for (std::size_t successor : info.successors) {
                new_out.insert(blocks[successor].live_in.begin(),
                               blocks[successor].live_in.end());
            }

            VRegSet new_in = info.use;
            for (int vreg : new_out) {
                if (info.def.count(vreg) == 0) {
                    new_in.insert(vreg);
                }
            }

            if (new_in != info.live_in || new_out != info.live_out) {
                info.live_in = std::move(new_in);
                info.live_out = std::move(new_out);
                changed = true;
            }
        }
    }

    return blocks;
}

void ExpireOldIntervals(std::vector<LiveInterval>& active,
                        int position,
                        std::vector<Register>& free_regs) {
    auto interval = active.begin();
    while (interval != active.end()) {
        if (interval->end >= position) {
            ++interval;
            continue;
        }
        free_regs.push_back(interval->phys);
        interval = active.erase(interval);
    }
    std::sort(active.begin(), active.end(),
              [](const LiveInterval& lhs, const LiveInterval& rhs) {
                  return lhs.end < rhs.end;
              });
}

}  // namespace

std::vector<LiveInterval> ComputeLiveIntervals(const MachineFunction& fn) {
    const auto block_liveness = AnalyzeBlocks(fn);
    std::unordered_map<int, LiveInterval> intervals;

    auto touch = [&](int vreg, int position) -> LiveInterval& {
        const auto existing = intervals.find(vreg);
        if (existing != intervals.end()) {
            existing->second.start = std::min(existing->second.start, position);
            existing->second.end = std::max(existing->second.end, position);
            return existing->second;
        }
        LiveInterval interval;
        interval.vreg = vreg;
        interval.start = position;
        interval.end = position;
        return intervals.emplace(vreg, interval).first->second;
    };

    for (int parameter : fn.param_vregs) {
        touch(parameter, 0).start = 0;
    }

    int position = 0;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        for (const auto& instruction : fn.blocks[i].instructions) {
            if (instruction.def >= 0) {
                touch(instruction.def, position);
            }
            for (int use : instruction.uses) {
                touch(use, position);
            }
            position += 2;
        }

        // A single linear interval cannot represent holes. Extending it to
        // the boundaries of every block where the value is live is therefore
        // conservative, but prevents a value needed on a CFG edge (especially
        // a loop backedge) from sharing a register with a definition in that
        // block.
        for (int vreg : block_liveness[i].live_in) {
            touch(vreg, block_liveness[i].start);
        }
        for (int vreg : block_liveness[i].live_out) {
            touch(vreg, block_liveness[i].end);
        }
    }

    std::vector<LiveInterval> result;
    result.reserve(intervals.size());
    for (const auto& [_, interval] : intervals) {
        result.push_back(interval);
    }
    std::sort(result.begin(), result.end(),
              [](const LiveInterval& lhs, const LiveInterval& rhs) {
                  if (lhs.start == rhs.start) {
                      return lhs.vreg < rhs.vreg;
                  }
                  return lhs.start < rhs.start;
              });
    return result;
}

AllocationResult LinearScanAllocate(const MachineFunction&        fn,
                                    const std::vector<Register>&  available) {
    auto intervals = ComputeLiveIntervals(fn);
    AllocationResult result;
    std::vector<LiveInterval> active;
    std::vector<Register> free_regs = available;

    for (auto interval : intervals) {
        bool has_phys = false;
        ExpireOldIntervals(active, interval.start, free_regs);

        if (free_regs.empty()) {
            std::sort(active.begin(), active.end(),
                      [](const LiveInterval& lhs, const LiveInterval& rhs) {
                          return lhs.end < rhs.end;
                      });
            if (!active.empty() && active.back().end > interval.end) {
                const auto spilled = active.back();
                active.pop_back();
                result.vreg_to_phys.erase(spilled.vreg);
                result.vreg_to_slot[spilled.vreg] = result.stack_slots++;
                interval.phys = spilled.phys;
                has_phys = true;
            } else {
                result.vreg_to_slot[interval.vreg] = result.stack_slots++;
                interval.spilled = true;
            }
        }

        if (interval.spilled) {
            continue;
        }
        if (!has_phys) {
            interval.phys = free_regs.back();
            free_regs.pop_back();
        }
        result.vreg_to_phys[interval.vreg] = interval.phys;
        active.push_back(interval);
        std::sort(active.begin(), active.end(),
                  [](const LiveInterval& lhs, const LiveInterval& rhs) {
                      return lhs.end < rhs.end;
                  });
    }

    return result;
}

}  // namespace polyglot::backends::x86_64
