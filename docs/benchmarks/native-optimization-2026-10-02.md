# ARM64 原生产物的优化与寄存器分配评估

## 结果摘要

36/36 个配置的完整 stdout 均正确，288 次执行中没有超时。每个配置包含 1 次预热和 7 次正式采样，共 252 个正式样本。编译器、polyld 和共享库的运行前后 SHA256 完全一致。

在本机的三个数值内核上，O2 的 linear-scan 和 graph-coloring 相对同一编译器的 stack 基线达到 **1.54–2.62 倍**运行速度。两种分配器的相对次序随内核变化；这组数据支持它们减少栈访问带来的收益，不能据此推断所有程序的表现。

| O2 内核 | stack 中位数 ms | linear 中位数 ms | graph 中位数 ms | stack / linear | stack / graph |
| --- | ---: | ---: | ---: | ---: | ---: |
| integer_loop | 1595.727 | 916.522 | 910.478 | 1.741 | 1.753 |
| recursive_calls | 137.935 | 89.476 | 86.505 | 1.542 | 1.595 |
| eight_arguments | 264.259 | 100.837 | 109.631 | 2.621 | 2.410 |

## 协议与边界

- 主机：macOS-27.0-arm64-arm-64bit。测量前，其他代理与根任务停止了构建和测试；测量期间未安排其他项目实验。
- 4 个独立 C++ 源程序：startup 输出检查；integer_loop 累加 300,000,000 次；recursive_calls 计算 fib(35)；eight_arguments 执行 10,000,000 次八参数函数调用。所有预期整数均在有符号 i64 范围内。
- 每个内核分别使用 O0/O2/O3 与 stack/linear-scan/graph-coloring，合计 4 × 3 × 3 = 36 个原生产物。
- stack 是当前 ARM64 emitter 关闭寄存器分配后的控制基线。三种模式使用同一源码、IR passes、链接器与运行时。linear 和 graph 都采用 CFG 活跃性分析、callee-saved 寄存器以及溢出栈槽；phi 边采用并行复制。
- 先编译全部产物，再进行 1 次预热和 7 次正式执行。每轮用种子 1729 的随机序列打乱 36 个配置的执行顺序；每一次执行都检查完整 stdout、stderr、退出状态与超时。
- 运行耗时包含进程启动。startup 中位数约为 5–10 ms，应作为启动成本的尺度；其 allocator 比值主要反映短程序与调度波动，不用于推断数值内核收益。
- 本报告给出中位数、最小值、最大值和样本标准差。7 次样本仍有明显波动，没有进行统计显著性检验。
- 每个产物只记录一次编译耗时；编译速度的结论需要独立编译性能实验。产物字节数为整个 Mach-O 文件大小，段和页对齐可能掩盖机器码大小变化。
- 当前验证平台为 macOS ARM64。这些有限内核不能证明其他架构或完整语言标准的一致性。

## 默认优化流水线

- O0：保留 frontend 必需的规范化与 SSA 工作，PassManager 不增加默认优化 passes。
- O1：带类型的常量折叠、复制传播、死代码消除、CFG 规范化、冗余 phi 消除和公共子表达式消除。
- O2：在 O1 上增加强度削减，随后再次执行公共子表达式和死代码消除。
- O3：在 O2 上增加逃逸分析，并再次执行常量折叠、复制传播和死代码消除。
- 实验性循环变换、向量化和内联继续保留为显式 API，未纳入默认流水线。

## 复现与原始证据

从仓库根目录执行。为保留旧样本，输出目录必须不存在。

```sh
python3 scripts/benchmark_native_optimization.py \
  --polyc build-release/polyc \
  --output build-release/native-optimization/2026-10-02-rerun \
  --loop-iterations 300000000 --call-iterations 10000000 --fibonacci 35 \
  --warmups 1 --repetitions 7
```

本次保留目录：`build-release/native-optimization/2026-10-02-full/`。`results.json` 包含源码与工具链哈希、逐次命令、完整输出、编译耗时、每个运行样本，以及所有汇总统计；各内核子目录保留源码和全部原生产物。

[九语言语义差分报告](native-differential-2026-10-02.md)另行记录 576 个优化/寄存器配置的正确性结果。

## 全部运行配置

速度比定义为同一内核和优化级别的 stack 中位数除以该模式中位数。大于 1 表示该次测量更快。

| 内核 | 优化 | 分配器 | 结果 | 中位数 ms | 最小 ms | 最大 ms | 标准差 ms | 速度比 | 字节数 |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| startup | O0 | stack | passed | 6.679 | 4.897 | 10.337 | 2.218 | 1.000 | 20704 |
| startup | O0 | linear-scan | passed | 8.383 | 3.881 | 10.525 | 2.247 | 0.797 | 20704 |
| startup | O0 | graph-coloring | passed | 6.452 | 4.298 | 10.122 | 1.853 | 1.035 | 20704 |
| startup | O2 | stack | passed | 5.228 | 3.560 | 9.645 | 2.090 | 1.000 | 20704 |
| startup | O2 | linear-scan | passed | 6.832 | 4.930 | 20.970 | 5.604 | 0.765 | 20704 |
| startup | O2 | graph-coloring | passed | 9.891 | 3.736 | 13.411 | 3.809 | 0.529 | 20704 |
| startup | O3 | stack | passed | 6.396 | 5.095 | 11.941 | 2.690 | 1.000 | 20704 |
| startup | O3 | linear-scan | passed | 5.635 | 3.296 | 9.624 | 2.226 | 1.135 | 20704 |
| startup | O3 | graph-coloring | passed | 7.220 | 3.793 | 12.933 | 3.048 | 0.886 | 20704 |
| integer_loop | O0 | stack | passed | 1611.489 | 1537.184 | 1686.106 | 57.549 | 1.000 | 20720 |
| integer_loop | O0 | linear-scan | passed | 967.526 | 834.885 | 1118.916 | 89.071 | 1.666 | 20720 |
| integer_loop | O0 | graph-coloring | passed | 938.048 | 798.482 | 1112.303 | 110.153 | 1.718 | 20720 |
| integer_loop | O2 | stack | passed | 1595.727 | 1513.861 | 1608.715 | 36.002 | 1.000 | 20720 |
| integer_loop | O2 | linear-scan | passed | 916.522 | 833.802 | 1027.329 | 63.286 | 1.741 | 20720 |
| integer_loop | O2 | graph-coloring | passed | 910.478 | 765.127 | 928.183 | 62.313 | 1.753 | 20720 |
| integer_loop | O3 | stack | passed | 1524.554 | 1459.406 | 1615.012 | 52.401 | 1.000 | 20720 |
| integer_loop | O3 | linear-scan | passed | 938.690 | 748.984 | 974.120 | 89.958 | 1.624 | 20720 |
| integer_loop | O3 | graph-coloring | passed | 810.254 | 720.481 | 950.550 | 97.364 | 1.882 | 20720 |
| recursive_calls | O0 | stack | passed | 114.374 | 94.637 | 152.671 | 22.341 | 1.000 | 20720 |
| recursive_calls | O0 | linear-scan | passed | 95.509 | 75.043 | 165.062 | 31.442 | 1.198 | 20720 |
| recursive_calls | O0 | graph-coloring | passed | 100.660 | 81.618 | 119.934 | 14.569 | 1.136 | 20720 |
| recursive_calls | O2 | stack | passed | 137.935 | 121.143 | 165.245 | 14.927 | 1.000 | 20720 |
| recursive_calls | O2 | linear-scan | passed | 89.476 | 58.027 | 112.563 | 17.727 | 1.542 | 20720 |
| recursive_calls | O2 | graph-coloring | passed | 86.505 | 62.762 | 116.086 | 17.478 | 1.595 | 20720 |
| recursive_calls | O3 | stack | passed | 122.232 | 84.358 | 145.536 | 19.665 | 1.000 | 20720 |
| recursive_calls | O3 | linear-scan | passed | 101.057 | 74.805 | 111.228 | 12.447 | 1.210 | 20720 |
| recursive_calls | O3 | graph-coloring | passed | 101.150 | 63.010 | 114.856 | 22.263 | 1.208 | 20720 |
| eight_arguments | O0 | stack | passed | 336.661 | 281.848 | 405.888 | 37.597 | 1.000 | 20752 |
| eight_arguments | O0 | linear-scan | passed | 118.778 | 86.263 | 149.267 | 18.716 | 2.834 | 20752 |
| eight_arguments | O0 | graph-coloring | passed | 118.007 | 89.647 | 148.724 | 21.109 | 2.853 | 20752 |
| eight_arguments | O2 | stack | passed | 264.259 | 239.163 | 338.642 | 36.620 | 1.000 | 20752 |
| eight_arguments | O2 | linear-scan | passed | 100.837 | 84.837 | 126.411 | 16.841 | 2.621 | 20752 |
| eight_arguments | O2 | graph-coloring | passed | 109.631 | 67.305 | 136.481 | 26.973 | 2.410 | 20752 |
| eight_arguments | O3 | stack | passed | 277.916 | 192.849 | 334.367 | 59.086 | 1.000 | 20752 |
| eight_arguments | O3 | linear-scan | passed | 115.756 | 70.441 | 141.120 | 26.756 | 2.401 | 20752 |
| eight_arguments | O3 | graph-coloring | passed | 103.268 | 67.226 | 134.519 | 22.418 | 2.691 | 20752 |
