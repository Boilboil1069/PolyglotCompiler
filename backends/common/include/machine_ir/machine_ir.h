/**
 * @file     machine_ir.h
 * @brief    Target-independent MachineIR data model and core code-generation
 *           algorithms (live-interval analysis, linear-scan / graph-coloring
 *           register allocation, list scheduling).
 *
 * Per-target headers (`backends/<target>/include/machine_ir.h`) declare a
 * `TargetTraits` struct exposing `Register` and a default register value, plus
 * a target-specific `Opcode` enum. They then alias the templates below into
 * the `polyglot::backends::<target>` namespace, so existing call sites do not
 * change.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace polyglot::backends::common::machine_ir {

/// @brief Selectable register-allocation strategy.
///
/// The enum lives in the shared namespace because the strategy is target-
/// independent. Each backend re-exports it via a `using` alias so that
/// previously-existing code that referenced `polyglot::backends::<target>::
/// RegAllocStrategy` keeps compiling.
enum class RegAllocStrategy { kLinearScan, kGraphColoring };

/// @brief Operand of a machine instruction.
///
/// Parameterised on a `TargetTraits` type that must expose:
///   * `using Register = ...;` — the per-target physical-register enum.
///   * `static constexpr Register kDefaultRegister = ...;` — value used when
///     `kind != kPhysReg` (the field is always present so the struct stays
///     trivially default-constructible).
template <typename TargetTraits>
struct Operand {
    using Register = typename TargetTraits::Register;

    enum class Kind { kVReg, kPhysReg, kImm, kLabel, kStackSlot, kMemVReg, kMemLabel };

    Kind         kind{Kind::kImm};
    int          vreg{-1};
    Register     phys{TargetTraits::kDefaultRegister};
    long long    imm{0};
    std::string  label;
    int          stack_slot{-1};
    bool         is_float{false};

    static Operand VReg(int v, bool is_float_op = false) {
        Operand op;
        op.kind     = Kind::kVReg;
        op.vreg     = v;
        op.is_float = is_float_op;
        return op;
    }

    static Operand Phys(Register r, bool is_float_op = false) {
        Operand op;
        op.kind     = Kind::kPhysReg;
        op.phys     = r;
        op.is_float = is_float_op;
        return op;
    }

    static Operand Imm(long long v) {
        Operand op;
        op.kind = Kind::kImm;
        op.imm  = v;
        return op;
    }

    static Operand Label(const std::string& name) {
        Operand op;
        op.kind  = Kind::kLabel;
        op.label = name;
        return op;
    }

    static Operand Stack(int slot) {
        Operand op;
        op.kind       = Kind::kStackSlot;
        op.stack_slot = slot;
        return op;
    }

    static Operand MemVReg(int v, bool is_float_op = false) {
        Operand op;
        op.kind     = Kind::kMemVReg;
        op.vreg     = v;
        op.is_float = is_float_op;
        return op;
    }

    static Operand MemLabel(const std::string& name) {
        Operand op;
        op.kind  = Kind::kMemLabel;
        op.label = name;
        return op;
    }
};

/// @brief Single machine instruction.
///
/// `OpcodeT` is the per-target opcode enum; it is intentionally not made part
/// of `TargetTraits` so that the per-target `Opcode` definitions can stay in
/// each backend's own header (they evolve independently).
template <typename TargetTraits, typename OpcodeT>
struct MachineInstr {
    OpcodeT                          opcode{};
    int                              def{-1};
    std::vector<int>                 uses;
    std::vector<Operand<TargetTraits>> operands;
    int                              cost{1};
    int                              latency{1};
    bool                             terminator{false};
};

template <typename TargetTraits, typename OpcodeT>
struct MachineBasicBlock {
    std::string                                       name;
    std::vector<MachineInstr<TargetTraits, OpcodeT>>  instructions;
};

template <typename TargetTraits, typename OpcodeT>
struct MachineFunction {
    std::string                                              name;
    std::vector<MachineBasicBlock<TargetTraits, OpcodeT>>    blocks;
};

/// @brief Live-range information for a virtual register.
template <typename TargetTraits>
struct LiveInterval {
    using Register = typename TargetTraits::Register;

    int       vreg{-1};
    int       start{0};
    int       end{0};
    bool      spilled{false};
    Register  phys{TargetTraits::kDefaultRegister};
    int       stack_slot{-1};
};

/// @brief Result of a register-allocation pass.
template <typename TargetTraits>
struct AllocationResult {
    using Register = typename TargetTraits::Register;

    std::unordered_map<int, Register> vreg_to_phys;
    std::unordered_map<int, int>      vreg_to_slot;
    int                               stack_slots{0};
};

// ---------------------------------------------------------------------------
//  Algorithms
// ---------------------------------------------------------------------------
//  All algorithms below are target-independent: they only depend on the shape
//  of `MachineFunction` and on the equality / vector-erase semantics of the
//  per-target `Register` value type. Implementations are kept inline in the
//  header so that target-specific instantiations do not require an explicit
//  template instantiation list in a .cpp file.
// ---------------------------------------------------------------------------

template <typename TargetTraits, typename OpcodeT>
inline std::vector<LiveInterval<TargetTraits>>
ComputeLiveIntervals(const MachineFunction<TargetTraits, OpcodeT>& fn) {
    std::unordered_map<int, LiveInterval<TargetTraits>> intervals;
    int position = 0;
    auto ensure  = [&](int vreg) -> LiveInterval<TargetTraits>& {
        auto it = intervals.find(vreg);
        if (it != intervals.end()) {
            return it->second;
        }
        LiveInterval<TargetTraits> li;
        li.vreg  = vreg;
        li.start = position;
        li.end   = position;
        auto [inserted_it, _] = intervals.emplace(vreg, li);
        return inserted_it->second;
    };

    for (const auto& bb : fn.blocks) {
        for (const auto& mi : bb.instructions) {
            if (mi.def >= 0) {
                auto& li = ensure(mi.def);
                li.start = std::min(li.start, position);
                li.end   = std::max(li.end, position);
            }
            for (int use : mi.uses) {
                auto& li = ensure(use);
                li.end   = std::max(li.end, position);
            }
            position += 2;  // leave gap for finer-grained scheduling
        }
    }

    std::vector<LiveInterval<TargetTraits>> out;
    out.reserve(intervals.size());
    for (auto& kv : intervals) {
        out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(),
              [](const LiveInterval<TargetTraits>& a,
                 const LiveInterval<TargetTraits>& b) { return a.start < b.start; });
    return out;
}

namespace detail {

template <typename TargetTraits>
inline void ExpireOldIntervals(std::vector<LiveInterval<TargetTraits>>& active,
                               int                                       position,
                               std::vector<typename TargetTraits::Register>& free_regs) {
    auto it = active.begin();
    while (it != active.end()) {
        if (it->end >= position) {
            ++it;
        } else {
            free_regs.push_back(it->phys);
            it = active.erase(it);
        }
    }
    std::sort(active.begin(), active.end(),
              [](const LiveInterval<TargetTraits>& a,
                 const LiveInterval<TargetTraits>& b) { return a.end < b.end; });
}

}  // namespace detail

template <typename TargetTraits, typename OpcodeT>
inline AllocationResult<TargetTraits>
LinearScanAllocate(const MachineFunction<TargetTraits, OpcodeT>&     fn,
                   const std::vector<typename TargetTraits::Register>& available) {
    auto                                       intervals = ComputeLiveIntervals(fn);
    AllocationResult<TargetTraits>             result;
    std::vector<LiveInterval<TargetTraits>>    active;
    std::vector<typename TargetTraits::Register> free_regs = available;

    for (auto interval : intervals) {
        bool has_phys = false;
        detail::ExpireOldIntervals<TargetTraits>(active, interval.start, free_regs);

        if (free_regs.empty()) {
            std::sort(active.begin(), active.end(),
                      [](const LiveInterval<TargetTraits>& a,
                         const LiveInterval<TargetTraits>& b) { return a.end < b.end; });
            if (!active.empty() && active.back().end > interval.end) {
                auto spilled_active = active.back();
                active.pop_back();
                result.vreg_to_slot[spilled_active.vreg] = result.stack_slots++;
                interval.phys = spilled_active.phys;
                has_phys      = true;
            } else {
                result.vreg_to_slot[interval.vreg] = result.stack_slots++;
                interval.spilled                   = true;
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
                free_regs.erase(std::remove(free_regs.begin(), free_regs.end(), interval.phys),
                                free_regs.end());
            }
            result.vreg_to_phys[interval.vreg] = interval.phys;
            active.push_back(interval);
            std::sort(active.begin(), active.end(),
                      [](const LiveInterval<TargetTraits>& a,
                         const LiveInterval<TargetTraits>& b) { return a.end < b.end; });
        }
    }

    return result;
}

namespace detail {

template <typename TargetTraits>
inline bool Overlaps(const LiveInterval<TargetTraits>& a,
                     const LiveInterval<TargetTraits>& b) {
    return !(a.end <= b.start || b.end <= a.start);
}

}  // namespace detail

template <typename TargetTraits, typename OpcodeT>
inline AllocationResult<TargetTraits>
GraphColoringAllocate(const MachineFunction<TargetTraits, OpcodeT>&     fn,
                      const std::vector<typename TargetTraits::Register>& available) {
    AllocationResult<TargetTraits> result;
    auto                            intervals = ComputeLiveIntervals(fn);
    if (intervals.empty()) {
        return result;
    }

    if (available.empty()) {
        for (const auto& li : intervals) {
            result.vreg_to_slot[li.vreg] = result.stack_slots++;
        }
        return result;
    }

    const std::size_t n = intervals.size();
    std::vector<std::vector<std::size_t>> graph(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (detail::Overlaps<TargetTraits>(intervals[i], intervals[j])) {
                graph[i].push_back(j);
                graph[j].push_back(i);
            }
        }
    }

    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        if (graph[lhs].size() == graph[rhs].size()) {
            return intervals[lhs].start < intervals[rhs].start;
        }
        return graph[lhs].size() > graph[rhs].size();
    });

    using Register = typename TargetTraits::Register;
    for (std::size_t idx : order) {
        std::vector<Register> used_regs;
        for (std::size_t neighbor : graph[idx]) {
            auto phys_it = result.vreg_to_phys.find(intervals[neighbor].vreg);
            if (phys_it != result.vreg_to_phys.end()) {
                used_regs.push_back(phys_it->second);
            }
        }

        Register chosen{TargetTraits::kDefaultRegister};
        bool     found = false;
        for (auto reg : available) {
            if (std::find(used_regs.begin(), used_regs.end(), reg) == used_regs.end()) {
                chosen = reg;
                found  = true;
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

namespace detail {

template <typename TargetTraits, typename OpcodeT>
inline void ScheduleBlock(MachineBasicBlock<TargetTraits, OpcodeT>& bb) {
    using Instr = MachineInstr<TargetTraits, OpcodeT>;
    std::vector<Instr> body;
    std::vector<Instr> terminators;
    for (auto& mi : bb.instructions) {
        if (mi.terminator) {
            terminators.push_back(mi);
        } else {
            body.push_back(mi);
        }
    }
    std::size_t n = body.size();
    if (n == 0) {
        bb.instructions = std::move(terminators);
        return;
    }

    std::vector<std::vector<int>> succ(n);
    std::vector<int>              indegree(n, 0);
    std::unordered_map<int, int>  last_def;

    for (std::size_t i = 0; i < n; ++i) {
        for (int use : body[i].uses) {
            auto it = last_def.find(use);
            if (it != last_def.end()) {
                succ[it->second].push_back(static_cast<int>(i));
                indegree[i] += 1;
            }
        }
        if (body[i].def >= 0) {
            last_def[body[i].def] = static_cast<int>(i);
        }
    }

    std::vector<Instr> scheduled;
    std::vector<int>   ready;
    for (std::size_t i = 0; i < n; ++i) {
        if (indegree[i] == 0) {
            ready.push_back(static_cast<int>(i));
        }
    }

    while (!ready.empty()) {
        auto best_it = std::max_element(ready.begin(), ready.end(), [&](int a, int b) {
            if (body[a].latency == body[b].latency) {
                return body[a].cost < body[b].cost;
            }
            return body[a].latency < body[b].latency;
        });
        int idx = *best_it;
        ready.erase(best_it);
        scheduled.push_back(body[idx]);
        for (int s : succ[idx]) {
            indegree[s] -= 1;
            if (indegree[s] == 0) {
                ready.push_back(s);
            }
        }
    }

    scheduled.insert(scheduled.end(), terminators.begin(), terminators.end());
    bb.instructions = std::move(scheduled);
}

}  // namespace detail

template <typename TargetTraits, typename OpcodeT>
inline void ScheduleFunction(MachineFunction<TargetTraits, OpcodeT>& fn) {
    for (auto& bb : fn.blocks) {
        detail::ScheduleBlock<TargetTraits, OpcodeT>(bb);
    }
}

// ---------------------------------------------------------------------------
//  Print: human-readable snapshot used by the verifier diagnostics.
// ---------------------------------------------------------------------------

template <typename TargetTraits, typename OpcodeT>
inline std::string Print(const MachineFunction<TargetTraits, OpcodeT>& fn) {
    std::ostringstream os;
    os << "function " << (fn.name.empty() ? std::string{"<anonymous>"} : fn.name) << " {\n";
    for (const auto& bb : fn.blocks) {
        os << "  " << (bb.name.empty() ? std::string{"<anonymous>"} : bb.name) << ":\n";
        for (std::size_t i = 0; i < bb.instructions.size(); ++i) {
            const auto& mi = bb.instructions[i];
            os << "    [" << i << "] op="
               << static_cast<long long>(static_cast<std::underlying_type_t<OpcodeT>>(mi.opcode))
               << " def=" << mi.def << " uses=[";
            for (std::size_t k = 0; k < mi.uses.size(); ++k) {
                if (k) os << ',';
                os << mi.uses[k];
            }
            os << "] term=" << (mi.terminator ? "yes" : "no") << "\n";
        }
    }
    os << "}\n";
    return os.str();
}

}  // namespace polyglot::backends::common::machine_ir
