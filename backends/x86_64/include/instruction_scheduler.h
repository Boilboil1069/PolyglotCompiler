/**
 * @file     instruction_scheduler.h
 * @brief    x86-64 code generation
 *
 * @ingroup  Backend / x86-64
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "backends/x86_64/include/machine_ir.h"

namespace polyglot::backends::x86_64 {

// Instruction Scheduler
// Reorders instructions to improve ILP and hide latency.

/** @brief InstructionScheduler class. */
class InstructionScheduler {
 public:
  explicit InstructionScheduler(const MachineFunction &func) : function_(func) {}

  // Run instruction scheduling
  MachineFunction Schedule();

 private:
  // Data-dependency graph node
  /** @brief SchedNode data structure. */
  struct SchedNode {
    MachineInstr *inst;
    std::vector<SchedNode *> predecessors;  // Dependency predecessors
    std::vector<SchedNode *> successors;    // Dependency successors
    int earliest_cycle{0};                  // Earliest schedulable cycle
    int latest_cycle{INT_MAX};              // Latest schedulable cycle
    int height{0};                          // Longest path to a leaf
    int depth{0};                           // Longest path from root
    bool scheduled{false};
  };

  // Build the dependency graph
  std::vector<std::unique_ptr<SchedNode>> BuildDependencyGraph(
      const std::vector<MachineInstr> &insts);

  // Compute critical path metrics
  void ComputeCriticalPath(std::vector<std::unique_ptr<SchedNode>> &nodes);

  // List-scheduling algorithm
  std::vector<MachineInstr> ListScheduling(
      std::vector<std::unique_ptr<SchedNode>> &nodes);

  // Heuristic: pick the next instruction to schedule
  SchedNode *SelectNext(const std::vector<SchedNode *> &ready_list);

  // Check readiness (all dependencies satisfied)
  bool IsReady(const SchedNode *node) const;

  // Update the ready list after scheduling a node
  void UpdateReadyList(std::vector<SchedNode *> &ready_list,
                      const SchedNode *scheduled);

  const MachineFunction &function_;
};

// Software Pipelining
// Advanced scheduling for loops.

/** @brief SoftwarePipeliner class. */
class SoftwarePipeliner {
 public:
  /** @brief PipelineStage data structure. */
  struct PipelineStage {
    std::vector<MachineInstr> instructions;
    int cycle{0};
  };

  /** @brief PipelineSchedule data structure. */
  struct PipelineSchedule {
    std::vector<PipelineStage> prologue;   // Prologue
    std::vector<PipelineStage> kernel;     // Kernel
    std::vector<PipelineStage> epilogue;   // Epilogue
    int initiation_interval{1};            // Initiation interval (II)
  };

  // Simplified loop info
  /** @brief LoopInfo data structure. */
  struct LoopInfo {
    int trip_count{-1};  // Trip count (-1 unknown)
    bool has_side_effects{false};
  };

  // Perform software pipelining on a loop
  static PipelineSchedule PipelineLoop(const std::vector<MachineInstr> &loop_body,
                                       const LoopInfo &loop_info);

 private:
  // Compute minimum initiation interval (MII)
  static int ComputeMII(const std::vector<MachineInstr> &body);

  // Modulo scheduling algorithm
  static PipelineSchedule ModuloScheduling(const std::vector<MachineInstr> &body,
                                           int target_ii);
};

// Instruction Fusion
// Merge simple instructions into more complex ones.

/** @brief InstructionFusion class. */
class InstructionFusion {
 public:
  // Detect and fuse instruction patterns
  static std::vector<MachineInstr> FuseInstructions(
      const std::vector<MachineInstr> &insts);

 private:
  // LEA fusion: add/mul -> lea
  static bool FuseToLEA(std::vector<MachineInstr> &insts, size_t pos);

  // Compare-branch fusion: cmp + jcc -> test/cmp with flags
  static bool FuseCmpJump(std::vector<MachineInstr> &insts, size_t pos);

  // Load-op fusion: load + add -> add [mem]
  static bool FuseLoadOp(std::vector<MachineInstr> &insts, size_t pos);

  // SIMD fusion: multiple scalar ops -> vector op
  static bool FuseToSIMD(std::vector<MachineInstr> &insts, size_t pos);
};

// Micro-op analysis
// Optimizations for specific CPU microarchitectures.

/** @brief MicroOpInfo data structure. */
struct MicroOpInfo {
  int num_uops{1};           // Number of micro-ops
  int port_mask{0};          // Executable port mask
  int latency{1};            // Latency in cycles
  int throughput{1};         // Throughput (reciprocal)
  bool can_dual_issue{false}; // Whether it can dual-issue
};

/** @brief MicroArchOptimizer class. */
class MicroArchOptimizer {
 public:
  // Optimize for a specific microarchitecture
  /** @brief Architecture enumeration. */
  enum Architecture {
    kGeneric,
    kHaswell,
    kSkylake,
    kZen2,
    kZen3
  };

  explicit MicroArchOptimizer(Architecture arch) : arch_(arch) {}

  // Optimize an instruction sequence
  std::vector<MachineInstr> Optimize(const std::vector<MachineInstr> &insts);

 private:
  // Retrieve micro-op info for an instruction
  MicroOpInfo GetMicroOpInfo(const MachineInstr &inst) const;

  // Avoid false dependencies
  void BreakFalseDependencies(std::vector<MachineInstr> &insts);

  // Balance port pressure
  void BalancePortPressure(std::vector<MachineInstr> &insts);

  // Avoid partial-register stalls
  void AvoidPartialRegisterStalls(std::vector<MachineInstr> &insts);

  // Optimize branch alignment
  void OptimizeBranchAlignment(std::vector<MachineInstr> &insts);

  Architecture arch_;
};

// Register Renaming optimization
// Eliminate WAR/WAW dependencies.

/** @brief RegisterRenamer class. */
class RegisterRenamer {
 public:
  // Rename registers to remove false dependencies
  static std::vector<MachineInstr> RenameRegisters(
      const std::vector<MachineInstr> &insts);

 private:
  // Analyze live ranges
  /** @brief LiveRange data structure. */
  struct LiveRange {
    int start;
    int end;
    int vreg;
  };

  static std::vector<LiveRange> ComputeLiveRanges(
      const std::vector<MachineInstr> &insts);

  // Find opportunities for renaming
  static bool FindRenameOpportunity(const LiveRange &range,
                                   const std::vector<LiveRange> &all_ranges);
};

// Zero-latency instruction optimization
// Identify and exploit zero-latency instructions (e.g., mov elimination).

/** @brief ZeroLatencyOptimizer class. */
class ZeroLatencyOptimizer {
 public:
  // Optimize zero-latency moves
  static std::vector<MachineInstr> OptimizeMoves(
      const std::vector<MachineInstr> &insts);

 private:
  // Detect mov instructions that can be eliminated
  static bool CanEliminateMove(const MachineInstr &mov);

  // Apply xor zero idiom
  static bool UseZeroIdiom(MachineInstr &inst);

  // Apply ones idiom (cmp/test with self)
  static bool UseOnesIdiom(MachineInstr &inst);
};

// Cache optimizations
// Improve memory access patterns.

/** @brief CacheOptimizer class. */
class CacheOptimizer {
 public:
  // Optimize data layout
  static void OptimizeDataLayout(MachineFunction &func);

  // Insert prefetch instructions
  static void InsertPrefetch(std::vector<MachineInstr> &insts,
                            const SoftwarePipeliner::LoopInfo &loop_info);

  // Improve cache-line alignment
  static void AlignCacheLines(MachineFunction &func);

 private:
  // Analyze memory access patterns
  /** @brief AccessPattern data structure. */
  struct AccessPattern {
    int stride;
    bool is_sequential;
    size_t access_size;
  };

  static AccessPattern AnalyzeAccessPattern(const std::vector<MachineInstr> &insts);

  // Compute prefetch distance
  static int ComputePrefetchDistance(const AccessPattern &pattern);
};

// Branch optimizations
// Improve branch instructions.

/** @brief BranchOptimizer class. */
class BranchOptimizer {
 public:
  // Optimize branches
  static std::vector<MachineInstr> OptimizeBranches(
      const std::vector<MachineInstr> &insts);

 private:
  // Convert conditional mov to cmov
  static bool ConvertToCMOV(std::vector<MachineInstr> &insts, size_t pos);

  // Invert branch condition to improve prediction
  static bool InvertBranch(MachineInstr &branch);

  // Eliminate unnecessary branches
  static bool EliminateBranch(std::vector<MachineInstr> &insts, size_t pos);
};

}  // namespace polyglot::backends::x86_64
