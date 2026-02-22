# 优化管线与开关矩阵

本文档描述 PolyglotCompiler 的优化基础设施，包括默认 Pass 管线、
`polyopt` 独立工具、LTO（链接时优化）、PGO（配置文件引导优化）以及
控制各功能的命令行开关。

---

## 1 概述

编译器的优化子系统完全位于 `middle/` 目录中：

| 层级 | 头文件 / 目录 | 用途 |
|------|--------------|------|
| 函数级 Pass | `middle/include/ir/passes/opt.h` | 常量折叠、DCE、拷贝传播、CSE、CFG 规范化、Mem2Reg |
| 变换 Pass | `middle/include/passes/transform/` | 内联、GVN、循环优化、高级优化（TCO、LICM、自动向量化等） |
| 分析 Pass | `middle/include/passes/analysis/` | 别名分析、支配树 |
| 去虚拟化 | `middle/include/passes/devirtualization.h` | 跨语言虚调用解析 |
| LTO | `middle/include/lto/link_time_optimizer.h` | 跨模块内联、过程间常量传播、全局 DCE、Thin LTO |

两个入口调用此基础设施：

1. **`polyc` 驱动** (`tools/polyc/src/driver.cpp`) — 在 SSA 构造后运行
   函数级默认 Pass，由 `-O0`..`-O3` 控制。
2. **`polyopt` 工具** (`tools/polyopt/src/optimizer.cpp`) — 独立 IR
   优化器，读取文本 IR，运行上下文级 Pass，输出结果。

---

## 2 默认函数级管线 (`opt.h`)

`polyglot::ir::passes::RunDefaultOptimizations(Function&)` 执行：

1. `ConstantFold` — 在编译时计算常量表达式。
2. `DeadCodeEliminate` — 删除不可达/未使用的指令。
3. `CopyProp` — 转发拷贝并消除冗余移动。
4. `CanonicalizeCFG` — 合并/简化基本块。
5. `EliminateRedundantPhis` — 折叠平凡 φ 节点。
6. `CSE` — 公共子表达式消除。
7. `Mem2Reg` — 将基于内存的局部变量提升为 SSA 寄存器。

当 `opt_level >= 1` 时，`polyc` 自动调用此管线。

---

## 3 上下文级 Pass（polyopt / polyc -O2+）

`polyglot::tools::Optimize(IRContext&)` 运行：

| Pass | 头文件 | 描述 |
|------|--------|------|
| `RunConstantFold` | `constant_fold.h` | 全上下文常量折叠 |
| `RunDeadCodeElimination` | `dead_code_elim.h` | 全上下文死代码删除 |
| `RunCommonSubexpressionElimination` | `common_subexpr.h` | 跨函数 CSE |
| `RunInlining` | `inlining.h` | 内联小函数/单调用点函数 |

---

## 4 高级 Pass (`advanced_optimizations.h`)

以下 Pass 可用于 `-O2`/`-O3` 和 LTO：

| Pass | 函数 | 类别 |
|------|------|------|
| 尾调用优化 | `TailCallOptimization` | 控制流 |
| 循环展开 | `LoopUnrolling` | 循环 |
| 软件流水线 | `SoftwarePipelining` | 循环 |
| 强度削减 | `StrengthReduction` | 算术 |
| 循环不变代码外提 | `LoopInvariantCodeMotion` | 循环 |
| 归纳变量消除 | `InductionVariableElimination` | 循环 |
| 部分求值 | `PartialEvaluation` | 常量 |
| 逃逸分析 | `EscapeAnalysis` | 内存 |
| 聚合体标量替换 | `ScalarReplacement` | 内存 |
| 死存储消除 | `DeadStoreElimination` | 内存 |
| 自动向量化 | `AutoVectorization` | SIMD |
| 循环融合 | `LoopFusion` | 循环 |
| 循环裂变 | `LoopFission` | 循环 |
| 循环交换 | `LoopInterchange` | 循环 |
| 循环分块 | `LoopTiling` | 循环/缓存 |
| 别名分析 | `AliasAnalysis` | 分析 |
| 稀疏条件常量传播 | `SCCP` | 常量 |
| 代码下沉 | `CodeSinking` | 调度 |
| 代码上提 | `CodeHoisting` | 调度 |
| 分支预测提示 | `BranchPrediction` | 控制流 |

---

## 5 GVN / PRE (`gvn.h`)

`GVNPass` 执行全局值编号，可选部分冗余消除（PRE）。按函数运行，在
`-O2` 时调用。

---

## 6 循环优化框架 (`loop_optimization.h`)

`LoopAnalysis` 检测自然循环、计算嵌套深度，并提供 `LoopInfo` 结构，
供循环 Pass（展开、LICM、分块、交换、融合、裂变）使用。

---

## 7 链接时优化 (`link_time_optimizer.h`)

LTO 在链接时跨翻译单元操作。主要功能：

| 功能 | 描述 |
|------|------|
| 跨模块内联 | 基于代价模型的跨 IR 模块函数内联 |
| 过程间常量传播 | 使用格（lattice）meet 操作的 IPCP |
| 全局死代码消除 | 从入口点不可达的函数/全局变量删除 |
| 去虚拟化 | 已知具体类型时解析虚/接口调用 |
| 全局值编号 | 跨模块冗余消除 |
| Thin LTO | 基于摘要的可扩展 LTO |

### 7.1 LTO 代价模型

内联代价模型平衡多个因素：

| 因素 | 默认值 |
|------|--------|
| 基础指令代价 | 每条指令 5 |
| 小函数奖励 | 50（阈值 ≤ 10 条指令） |
| 单调用点奖励 | 75 |
| 热调用点奖励（PGO） | 100 |
| 递归惩罚 | 200 |
| 复杂度惩罚 | 每基本块 2 |

### 7.2 PGO 集成

当配置文件数据可用时，LTO Pass 调整内联阈值：

- 热调用点获得 `kHotCallSiteBonus`（100），降低有效代价阈值，使内联
  更有可能发生。
- 配置文件计数驱动循环展开因子选择。
- 为支持的后端发出分支预测提示。

---

## 8 开关矩阵

| 标志 | `polyc` | `polyopt` | 效果 |
|------|---------|-----------|------|
| `-O0` | ✓ | ✓ | 无优化 |
| `-O1` | ✓ | ✓ | 基础：常量折叠 + DCE |
| `-O2` | ✓ | ✓ | 标准：+ CSE + 内联 |
| `-O3` | ✓ | ✓ | 激进：与 -O2 相同（保留供未来使用） |
| `--lto` | ✓ | — | 启用链接时优化 |
| `--emit-ir <path>` | ✓ | — | 将优化后的 IR 转储到文件 |
| `-o <file>` | ✓ | ✓ | 输出路径 |

### 8.1 polyopt 用法

```bash
polyopt [options] <input.ir> [-o <output.ir>]
```

| 选项 | 默认 | 描述 |
|------|------|------|
| `-O0` | — | 禁用所有 Pass |
| `-O1` | — | 仅常量折叠 + DCE |
| `-O2` | ✓ | 完整管线 |
| `-O3` | — | 与 -O2 相同 |
| `-o <file>` | stdout | 输出文件 |

---

## 9 添加新 Pass

1. 在 `middle/include/passes/transform/` 中声明 Pass 函数。
2. 在 `middle/src/passes/transform/` 中实现。
3. 在 `polyglot::tools::Optimize()` 或函数级管线中注册。
4. 在 `tests/unit/middle/` 中添加单元测试。
5. 如果 Pass 有 CLI 开关，在 `polyopt` 和 `polyc` 中接入。

---

## 10 路线图

- 将 PGO 配置文件读取器集成到 `polyc` CLI。
- 暴露每个 Pass 的计时诊断（`-ftime-report`）。
- 实现 `-O3` 差异化（自动向量化、软件流水线）。
- 添加 LTO 分区以支持并行优化。
