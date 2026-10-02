# 原生运行时、语义差分、优化与工作区更新

## 实现范围

本轮把编译正确性、基础运行时、优化和编辑工作区一起接入实际程序验证。执行平台为 macOS ARM64；所有原生产物均由项目的前端、后端及 `polyld` 生成。

### 编译正确性与入口

- 九种语言共用八类差分内核：正负整数、较大整数、嵌套调用、嵌套循环、递归、参数更新、短路条件和浮点数。
- 比较完整 stdout。整数不再依赖进程退出码的低八位；浮点输出使用精确十六进制格式，逐位比较 binary64，包括负零。
- 修复 phi 值合流、循环交换变量时的活跃性分析、参数写回、临时名冲突、浮点常量与负号、各前端短路逻辑，以及 JavaScript 括号解析。
- 标准入口回归覆盖 C++ `int main(int argc, char** argv)`、Java `static void main(String[] args)`、C# `static void Main(string[] args)`、Rust `fn main()` 和 Go `func main()`。参数可通过统一 `args_count/arg_text/arg_int` 读取；这不包含完整 JVM/CLR 字符串数组对象实现。
- 修复 ARM64 ADRP/ADD 重定位类型和最终 Mach-O 页布局下的地址修补。独立语言与 Poly 共用保留只读数据段的 Mach-O 对象写入器，字符串和全局变量地址得到正确链接。

### 原生运行时

新增显式 API 支持：i64/f64 输出、UTF-8 文本、命令行参数、i64 数组、文件扫描和写入。数组边界、参数格式与整数溢出均有确定的返回值；详见 [API 与示例](../examples/native_runtime/README.md)。

`examples/native_runtime/number_stats.poly` 是实际文件处理程序：从输入文件读取整数、插入排序、写出排序结果并打印统计。九种语言的 `runtime.*` 分别覆盖同一组 API；每份都可独立编译为可执行文件。

运行时 ARM64 指令源在 `runtime/src/libs/native/arm64_runtime.S`。`scripts/generate_native_runtime.py --check` 检查提交的 Darwin/Linux 编码与源文件一致；生成步骤验证没有遗漏的文本重定位。日常运行 `polyc` 不调用汇编器生成这些字节。当前本机执行证据限于 Darwin ARM64。

### 优化与寄存器分配

ARM64 使用 CFG 活跃性分析，将跨调用值放入 x19–x28，并在需要时溢出到栈。支持 `--regalloc=linear-scan` 与 `--regalloc=graph-coloring`。新增 `--regalloc=stack` 作为同一正确后端的纯栈基线；只适用于 ARM64。Poly 驱动也真实传递此参数。

| 等级 | 当前默认处理 |
| --- | --- |
| O0 | 保留基础 IR，不运行默认优化序列 |
| O1 | 保留类型和位模式的常量折叠、复制传播、死代码删除、CFG 整理、冗余 phi 清理、公共子表达式消除 |
| O2 | 在 O1 上增加可验证的强度削减与清理 |
| O3 | 增加逃逸分析及常量/复制/死代码清理 |

循环变换、向量化和激进内联等旧实验 pass 没有重新放入默认路径。这里的性能对比使用相同源码、编译器、优化等级和链接器，只改变寄存器分配策略；不能把结果解释为相对官方编译器的加速比。

### UI

- 真实 Qt 工作区提供 Code / Split / Flow 布局，可调分隔条、文件区、主编辑器、图画布和底部工具区。
- Poly 调用处可查看外部函数的签名、源码注释与源码预览；支持右键转到定义、F12、Ctrl+单击和预览中的打开定义按钮。
- 图为每次函数调用保留独立卡片，输入和输出端口显示参数名、类型与方向，连线显示变量和类型转换；运算、常量、显式转换及函数边界有独立节点。
- 图布局在边建立后执行，区分调用实例，并支持检查器、平移、缩放和避开无关卡片的连线。

详细操作与范围见 [工作区指南](UI_CROSS_LANGUAGE_WORKSPACE_zh.md) 和 [数据流指南](UI_VALUE_FLOW_zh.md)。图显示源码中的静态依赖和类型转换，不显示虚构的运行时取值。

## 验证命令

本机已经编译并实际运行的九种语言示例集中保存在 `build-release/native-examples/`，
其中 `manifest.json` 记录来源、SHA-256 和验证参数。可直接运行：

```bash
# 在项目根目录执行；每个 runtime-* 程序会在当前目录写入 native-values.txt。
build-release/native-examples/runtime-python hello -9223372036854775808 9223372036854775808 12x
build-release/native-examples/number-stats examples/native_runtime/numbers.txt build-release/sorted.txt

# 打开真实的跨语言编辑工作区。
build-release/polyui.app/Contents/MacOS/polyui \
  --folder examples/editor_cross_language_demo \
  --file examples/editor_cross_language_demo/order_flow.poly \
  --view split --peek subtotal
```

重新构建与验证：

```bash
cmake --build build-release --target polyc polyld polyui -j8
ctest --test-dir build-release -R 'native_(standalone|runtime|standard_entry|differential)_regression' --output-on-failure
ctest --test-dir build-release -R 'test_(cross_language_navigation|topology_ui|polyui_workspace_smoke)' --output-on-failure
python3 scripts/differential_native.py --polyc build-release/polyc \
  --output build-release/native-differential/full-001
python3 scripts/benchmark_native_optimization.py --polyc build-release/polyc \
  --output build-release/native-optimization/full-001 \
  --loop-iterations 300000000 --call-iterations 10000000 --fibonacci 35 \
  --opt-levels 0 2 3 --warmups 1 --repetitions 7
python3 scripts/benchmark_native.py --polyc build-release/polyc \
  --output build-release/native-evaluation/full-001 \
  --opt-levels 0 1 2 3 --scales 8 64 256 --warmups 1 --repetitions 7 --timeout 30
```

差分脚本可通过 `--node /path/to/node` 指定已安装的 Node。官方工具链缺失会明确记录为跳过；`--require-reference` 可让缺失变成测试失败。Poly 没有另一套官方实现，使用独立计算的结果预言值。

## 验证记录

2026-10-02，本机已完成：

| 验证 | 实际结果 |
| --- | --- |
| 语义差分 | 9 语言 × 8 内核 × 4 优化档 × 2 分配器，576/576 通过 |
| 官方实现对照 | 6 种已安装工具链执行 48 个参考程序，384/384 配置与其完整输出一致 |
| 原生运行时 | 72/72 配置的退出码、stdout、stderr 和实际文件内容全部通过 |
| 标准入口 | 5 种语言 × O0–O3，20/20 通过 |
| 原生标量回归 | 144 个编译运行配置及入口/打印/复合赋值检查通过 |
| 模块与 UI 回归 | 本轮选定的 20 个 CTest 目标全部通过 |
| 图界面 | 35 个用例，474 个断言通过；含三张真实 Qt 截图保存 |
| 文档与定义跳转 | C++ 与 Python 实际内联文档、源码预览、右键 QAction 跳转检查通过 |
| 有用程序 | 文件排序统计示例输出 8 / 1047 / -21 / 1000，排序文件逐项核对正确 |
| 编译性能矩阵 | 216/216 配置、1,728/1,728 次编译与实际执行通过 |
| 运行性能矩阵 | 36/36 配置、288/288 次执行完整输出正确，零超时 |

官方参考为 Apple Clang 21.0.0、CPython 3.9.6、OpenJDK 21.0.8、.NET SDK 8.0.412、Node 24.19.0 和 Ruby 2.6.10。Rust/Go 工具链未安装，Poly 没有独立官方实现；192 个相关配置通过独立数值预言值检查，官方对照明确跳过。

[完整差分报告](benchmarks/native-differential-2026-10-02.md) 保存所有配置状态。原始源码、二进制、命令与日志位于 `build-release/native-differential/2026-10-02-full/`；运行前后编译器和动态库哈希一致。运行时结果在 `build-release/native-runtime/2026-10-02-full/results.json`，标准入口在 `build-release/native-entries/entries-sojtfu9x/results.json`。

## 性能测量

### 九种语言的编译性能

9 种语言 × 6 个工作负载 × O0/O1/O2/O3，共 **216/216 配置通过**。
每配置预热一次、正式测量七次；总计 **1,728 次编译并执行**，其中 1,512 次为正式样本。
工作负载包括嵌套调用、条件与循环、递归，以及 8/64/256 个辅助函数。
每次使用新的产物路径，保留源码、命令、日志和实际可执行文件。

O2 下，九种语言调用样例的编译中位数范围为 **28.7–37.9 ms**，
256 函数样例为 **45.3–60.0 ms**。这些是各配置七个样本的中位数范围，
包含驱动、编译与链接耗时，不用于给语言排名。操作系统文件缓存保持自然状态。
短程序运行时间还包含首次启动新 Mach-O 产物的开销，计算吞吐由下节的长内核单独测量。

本实验与运行性能实验顺序执行，没有并行构建或测试。
完成后再次检查编译器、链接器及所有同目录动态库，SHA-256 均与开始时一致。
逐语言表格、全部配置、复现命令及统计口径见
[本轮编译性能报告](benchmarks/native-2026-10-02-runtime-upgrade.md)。
原始样本位于 `build-release/native-evaluation/2026-10-02-runtime-upgrade/results.json`；
完成计数和工具链检查保存在同目录 `verification.json`。

### 生成程序的运行性能

四个内核 × O0/O2/O3 × 三种寄存器策略，共 **36/36 配置通过**。
每配置预热一次、正式测量七次；全部 **288 次执行**核对完整 stdout，零超时。
各轮以固定随机种子打乱配置顺序。实验期间没有并行构建或回归测试，
编译器、链接器与动态库的前后 SHA-256 一致。

O2 的运行中位数如下，时间包含进程启动：

| 工作负载 | 纯栈（ms） | 线性扫描（ms） | 图着色（ms） | 纯栈 / 线性扫描 | 纯栈 / 图着色 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 3 亿次整数循环 | 1595.727 | 916.522 | 910.478 | 1.74× | 1.75× |
| 1000 万次八参数调用 | 264.259 | 100.837 | 109.631 | 2.62× | 2.41× |
| fib(35) 递归 | 137.935 | 89.476 | 86.505 | 1.54× | 1.59× |
| 启动与单次输出 | 5.228 | 6.832 | 9.891 | 0.77× | 0.53× |

启动内核没有足够的计算工作来抵消启动波动，寄存器分配策略在这里没有一致收益。
长内核的结果也只描述本机、这些程序与当前编译器；不能推广为所有语言或所有程序的加速，
也不是相对官方编译器的比较。每个产物只记录一次编译时间，该实验不用于估计编译加速。

[完整运行性能报告](benchmarks/native-optimization-2026-10-02.md) 包含全部配置、样本分布和口径。
原始记录与可执行文件保存在 `build-release/native-optimization/2026-10-02-full/`。

## 尚未覆盖的范围

本轮验证的是各语言已实现的静态原生子集，未实现完整标准库、对象字符串运行时、异常、任意精度整数或全部语言语法。原生 Mach-O 外部对象的指令内 addend、ARM64_RELOC_ADDEND 及其他重定位种类也没有完整覆盖；本轮程序使用已验证的符号地址与 ADRP/ADD/BL 组合。跨系统执行结论限于已实测的 macOS ARM64。
