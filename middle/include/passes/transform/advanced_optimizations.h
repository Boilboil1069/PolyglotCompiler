  #pragma once

#include "middle/include/ir/cfg.h"

namespace polyglot::passes::transform {

// 尾调用优化（Tail Call Optimization）
// 将尾递归转换为循环，避免栈溢出
void TailCallOptimization(ir::Function &func);

// 循环展开（Loop Unrolling）
// 展开循环以减少循环开销和提高ILP
void LoopUnrolling(ir::Function &func, size_t factor = 4);

// 软件流水线（Software Pipelining）
// 重叠循环迭代以提高性能
void SoftwarePipelining(ir::Function &func);

// 强度削减（Strength Reduction）
// 将昂贵的操作替换为等价的廉价操作（如乘法→加法）
void StrengthReduction(ir::Function &func);

// 循环不变代码外提（Loop-Invariant Code Motion）
// 将循环内不变的计算移到循环外
void LoopInvariantCodeMotion(ir::Function &func);

// 归纳变量消除（Induction Variable Elimination）
// 简化或消除循环归纳变量
void InductionVariableElimination(ir::Function &func);

// 部分求值（Partial Evaluation）
// 在编译时执行部分计算
void PartialEvaluation(ir::Function &func);

// 逃逸分析（Escape Analysis）
// 分析对象是否逃逸到函数外，用于栈分配优化
void EscapeAnalysis(ir::Function &func);

// 标量替换（Scalar Replacement of Aggregates）
// 将结构体/数组分解为独立的标量变量
void ScalarReplacement(ir::Function &func);

// 死存储消除（Dead Store Elimination）
// 移除永远不会被读取的存储操作
void DeadStoreElimination(ir::Function &func);

// 自动向量化（Auto-Vectorization）
// 自动将标量操作转换为SIMD向量操作
void AutoVectorization(ir::Function &func);

// 循环融合（Loop Fusion）
// 合并相邻的循环以提高缓存局部性
void LoopFusion(ir::Function &func);

// 循环分裂（Loop Fission）
// 将一个循环分裂为多个以提高并行性
void LoopFission(ir::Function &func);

// 循环交换（Loop Interchange）
// 交换嵌套循环的顺序以改善缓存性能
void LoopInterchange(ir::Function &func);

// 循环分块（Loop Tiling）
// 将循环分成块以提高缓存利用率
void LoopTiling(ir::Function &func, size_t tile_size = 64);

// 别名分析（Alias Analysis）
// 分析指针别名关系
void AliasAnalysis(ir::Function &func);

// 稀疏条件常量传播（Sparse Conditional Constant Propagation）
// 基于SSA的高级常量传播
void SCCP(ir::Function &func);

// 代码沉降（Code Sinking）
// 将计算移动到离使用点更近的位置
void CodeSinking(ir::Function &func);

// 代码提升（Code Hoisting）
// 将公共代码提升到支配节点
void CodeHoisting(ir::Function &func);

// 跳转线程化（Jump Threading）
// 优化跳转链和分支预测
void JumpThreading(ir::Function &func);

// 全局值编号（Global Value Numbering）
// 识别等价表达式并复用计算结果
void GVN(ir::Function &func);

// 预取插入（Prefetch Insertion）
// 插入预取指令以隐藏内存延迟
void PrefetchInsertion(ir::Function &func);

// 分支预测优化（Branch Prediction Optimization）
// 根据profile数据优化分支布局
void BranchPredictionOptimization(ir::Function &func);

// 循环谓词化（Loop Predication）
// 使用谓词执行消除循环中的分支
void LoopPredication(ir::Function &func);

// 内存布局优化（Memory Layout Optimization）
// 优化数据结构布局以提高缓存性能
void MemoryLayoutOptimization(ir::Function &func);

}  // namespace polyglot::passes::transform
