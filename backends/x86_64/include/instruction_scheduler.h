#pragma once

#include <memory>
#include <string>
#include <vector>

#include "backends/x86_64/include/machine_ir.h"

namespace polyglot::backends::x86_64 {

// 指令调度器（Instruction Scheduler）
// 重新排列指令以提高ILP（指令级并行）和隐藏延迟

class InstructionScheduler {
 public:
  explicit InstructionScheduler(const MachineFunction &func) : function_(func) {}

  // 执行指令调度
  MachineFunction Schedule();

 private:
  // 数据依赖图节点
  struct SchedNode {
    MachineInstr *inst;
    std::vector<SchedNode *> predecessors;  // 数据依赖前驱
    std::vector<SchedNode *> successors;    // 数据依赖后继
    int earliest_cycle{0};                  // 最早可调度周期
    int latest_cycle{INT_MAX};              // 最晚可调度周期
    int height{0};                          // 到叶节点的最长路径
    int depth{0};                           // 从根节点的最长路径
    bool scheduled{false};
  };

  // 构建数据依赖图
  std::vector<std::unique_ptr<SchedNode>> BuildDependencyGraph(
      const std::vector<MachineInstr> &insts);

  // 计算关键路径
  void ComputeCriticalPath(std::vector<std::unique_ptr<SchedNode>> &nodes);

  // 列表调度算法
  std::vector<MachineInstr> ListScheduling(
      std::vector<std::unique_ptr<SchedNode>> &nodes);

  // 启发式：选择下一个要调度的指令
  SchedNode *SelectNext(const std::vector<SchedNode *> &ready_list);

  // 检查指令是否就绪（所有依赖已满足）
  bool IsReady(const SchedNode *node) const;

  // 更新就绪列表
  void UpdateReadyList(std::vector<SchedNode *> &ready_list,
                      const SchedNode *scheduled);

  const MachineFunction &function_;
};

// 软件流水线（Software Pipelining）
// 针对循环的高级调度技术

class SoftwarePipeliner {
 public:
  struct PipelineStage {
    std::vector<MachineInstr> instructions;
    int cycle{0};
  };

  struct PipelineSchedule {
    std::vector<PipelineStage> prologue;   // 序言
    std::vector<PipelineStage> kernel;     // 核心循环
    std::vector<PipelineStage> epilogue;   // 尾声
    int initiation_interval{1};            // 启动间隔（II）
  };

  // 对循环执行软件流水线
  static PipelineSchedule PipelineLoop(const std::vector<MachineInstr> &loop_body,
                                       const LoopInfo &loop_info);

 private:
  // 计算最小启动间隔（MII）
  static int ComputeMII(const std::vector<MachineInstr> &body);

  // 模调度算法
  static PipelineSchedule ModuloScheduling(const std::vector<MachineInstr> &body,
                                           int target_ii);
};

// 指令融合（Instruction Fusion）
// 将多个简单指令融合为复杂指令

class InstructionFusion {
 public:
  // 检测并融合指令模式
  static std::vector<MachineInstr> FuseInstructions(
      const std::vector<MachineInstr> &insts);

 private:
  // LEA融合：add/mul -> lea
  static bool FuseToLEA(std::vector<MachineInstr> &insts, size_t pos);

  // 比较-跳转融合：cmp + jcc -> test/cmp with flags
  static bool FuseCmpJump(std::vector<MachineInstr> &insts, size_t pos);

  // 加载-运算融合：load + add -> add [mem]
  static bool FuseLoadOp(std::vector<MachineInstr> &insts, size_t pos);

  // SIMD融合：多个标量操作 -> 向量操作
  static bool FuseToSIMD(std::vector<MachineInstr> &insts, size_t pos);
};

// 微操作（Micro-ops）分析
// 针对特定CPU架构优化

struct MicroOpInfo {
  int num_uops{1};           // 微操作数量
  int port_mask{0};          // 可执行的端口掩码
  int latency{1};            // 延迟周期
  int throughput{1};         // 吞吐量（倒数）
  bool can_dual_issue{false}; // 是否可双发射
};

class MicroArchOptimizer {
 public:
  // 针对特定微架构优化
  enum Architecture {
    kGeneric,
    kHaswell,
    kSkylake,
    kZen2,
    kZen3
  };

  explicit MicroArchOptimizer(Architecture arch) : arch_(arch) {}

  // 优化指令序列
  std::vector<MachineInstr> Optimize(const std::vector<MachineInstr> &insts);

 private:
  // 获取指令的微操作信息
  MicroOpInfo GetMicroOpInfo(const MachineInstr &inst) const;

  // 避免错误依赖（False Dependencies）
  void BreakFalseDependencies(std::vector<MachineInstr> &insts);

  // 端口压力平衡
  void BalancePortPressure(std::vector<MachineInstr> &insts);

  // 避免部分寄存器写入停顿
  void AvoidPartialRegisterStalls(std::vector<MachineInstr> &insts);

  // 优化分支对齐
  void OptimizeBranchAlignment(std::vector<MachineInstr> &insts);

  Architecture arch_;
};

// 寄存器重命名（Register Renaming）优化
// 消除WAR和WAW依赖

class RegisterRenamer {
 public:
  // 重命名寄存器以消除虚假依赖
  static std::vector<MachineInstr> RenameRegisters(
      const std::vector<MachineInstr> &insts);

 private:
  // 分析活跃范围
  struct LiveRange {
    int start;
    int end;
    int vreg;
  };

  static std::vector<LiveRange> ComputeLiveRanges(
      const std::vector<MachineInstr> &insts);

  // 寻找可重命名的机会
  static bool FindRenameOpportunity(const LiveRange &range,
                                   const std::vector<LiveRange> &all_ranges);
};

// 零延迟指令优化
// 识别和利用零延迟指令（如mov elimination）

class ZeroLatencyOptimizer {
 public:
  // 优化零延迟移动
  static std::vector<MachineInstr> OptimizeMoves(
      const std::vector<MachineInstr> &insts);

 private:
  // 检测可以被消除的mov指令
  static bool CanEliminateMove(const MachineInstr &mov);

  // 使用xor zero idiom
  static bool UseZeroIdiom(MachineInstr &inst);

  // 使用ones idiom (cmp/test with self)
  static bool UseOnesIdiom(MachineInstr &inst);
};

// 缓存优化
// 优化内存访问模式

class CacheOptimizer {
 public:
  // 优化数据布局
  static void OptimizeDataLayout(MachineFunction &func);

  // 插入预取指令
  static void InsertPrefetch(std::vector<MachineInstr> &insts,
                            const LoopInfo &loop_info);

  // 优化缓存行对齐
  static void AlignCacheLines(MachineFunction &func);

 private:
  // 分析访问模式
  struct AccessPattern {
    int stride;
    bool is_sequential;
    size_t access_size;
  };

  static AccessPattern AnalyzeAccessPattern(const std::vector<MachineInstr> &insts);

  // 计算预取距离
  static int ComputePrefetchDistance(const AccessPattern &pattern);
};

// 分支优化
// 优化分支指令

class BranchOptimizer {
 public:
  // 优化分支
  static std::vector<MachineInstr> OptimizeBranches(
      const std::vector<MachineInstr> &insts);

 private:
  // 将条件mov转换为cmov
  static bool ConvertToCMOV(std::vector<MachineInstr> &insts, size_t pos);

  // 反转分支条件以改善预测
  static bool InvertBranch(MachineInstr &branch);

  // 消除不必要的分支
  static bool EliminateBranch(std::vector<MachineInstr> &insts, size_t pos);
};

}  // namespace polyglot::backends::x86_64
