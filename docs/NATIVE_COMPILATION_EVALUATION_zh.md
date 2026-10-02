# 九种语言的独立原生编译与性能评估（初版基线）

本文保留第一轮标量编译基线和实验协议。新增运行时、标准入口、语义差分、寄存器分配和 UI 的当前状态见 [本轮实现与验证](NATIVE_RUNTIME_UI_EVALUATION_zh.md)。下文无参数入口和纯栈实现的描述对应初版基线。

## 初版交付内容

`polyc` 现在可以把九种语言中已实现的静态标量源码子集编译、链接为独立的原生可执行文件。此次实际执行验证平台是 **macOS ARM64**。运行产物不需要 Python、JVM、CLR、Node、Ruby 或 Go/Rust 工具链。

这次贯通了源码入口选择、IR 常量、跨基本块变量存储、SSA 值保存、参数与返回值、递归调用、条件跳转及栈帧恢复。ARM64 汇编输出与目标文件共用机器码生成逻辑，输出精确字节和符号重定位表达式，可交给系统汇编器处理。`PRINTLN` 在实际调用位置执行系统调用，分支和循环决定输出次数，入口返回值决定进程退出码。

### 覆盖范围

| 输入 | 扩展名 | 原生入口及类型约束 |
| --- | --- | --- |
| Poly | `.poly` | `FUNC main() -> INT` |
| C++ | `.cpp` | 无参数 `main`，整数或 void 返回 |
| Python | `.py` | 带类型注解的无参数 `main` |
| Rust | `.rs` | 无参数 `main`，本项目支持整数返回入口 |
| Go | `.go` | `package main`，本项目支持整数返回入口 |
| Java | `.java` | 类内无参数 `static main` |
| C# | `.cs` | 类内无参数 `static Main` |
| JavaScript | `.js` | JSDoc 参数/返回类型；示例入口返回布尔校验值 |
| Ruby | `.rb` | YARD 参数/返回类型；顶层方法 `main` |

九种语言统一覆盖：有参数的多函数调用、条件分支、可变局部变量、循环、递归、逐级增加函数数量的源码。Java/C# 当前新增调用路径限于可静态解析的本地静态方法。浮点数由 JavaScript 样例覆盖，其余样例使用有界整数。

这些是 **polyc 自己的原生入口约定**。示例中 C++ 的 `long main`、Rust/Go 的整数返回 `main`、Java 的无参数 `main` 不等同于对应官方工具链的标准入口；C++ 的标准 `int main()` 也受支持，并由复合赋值回归覆盖。入口必须无参数且返回整数、布尔或 void；void 映射到退出码 0。POSIX 退出码保留低 8 位。`--entry=Program::Main` 等选项可显式指定源码函数。缺失、歧义或不支持的入口会报错；`-c` 不要求入口。

## 构建与运行

在仓库根目录执行：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target polyc polyld -j 8
mkdir -p build-release/native-examples
build-release/polyc --strict --no-package-index -O2 \
  examples/standalone_native/calls.cpp -o build-release/native-examples/calls
./build-release/native-examples/calls
echo $? # 42
```

[九种独立源码](../examples/standalone_native/)使用相同计算逻辑。替换扩展名即可编译其他语言。JavaScript 示例以布尔返回值验证 `42`，正确退出码为 1。非零退出码在这些测试中是有意设计的计算校验值。

仅生成目标文件及汇编：

```bash
build-release/polyc --strict -c examples/standalone_native/calls.cpp \
  -o build-release/native-examples/calls.o \
  --emit-asm=build-release/native-examples/calls.s
```

## 持续回归

```bash
ctest --test-dir build-release -R native_standalone_regression --output-on-failure
```

CMake 在 ARM64 Unix 原生构建、存在 Python 3 时注册测试；交叉编译时不自动运行目标产物。回归运行九种语言 × 四个工作负载 × 四档优化，共 **144 个编译并执行配置**，另检查自定义入口、非法入口、无入口目标文件、带空格路径、循环/分支打印和复合赋值。每次运行的源码、日志、产物都保存在 `build-release/native-regression/` 的独立目录，失败时可复现。

## 完整性能协议

```bash
python3 scripts/benchmark_native.py \
  --polyc build-release/polyc \
  --output build-release/native-evaluation/run-001 \
  --opt-levels 0 1 2 3 --scales 8 64 256 \
  --warmups 1 --repetitions 7 --timeout 30
```

输出目录必须是新目录，已有结果不会被覆盖。脚本仅使用 Python 3 标准库。

- 九种语言；六个工作负载：嵌套调用、循环与分支、Fibonacci 递归、8/64/256 个辅助函数的调用链。
- 四档优化；共 **216 个配置**。每配置预热 1 次、测量 7 次，共 **1,728 次编译并执行**。
- 每次编译使用新产物路径、`--strict --no-package-index --no-aux`，编译器与链接器均来自指定构建目录。
- 编译成功后检查原生文件头，再实际执行。只有退出码、stdout、stderr 都符合独立计算的预期结果，该次样本才算通过。任意失败使配置失败，不生成该配置性能汇总，脚本最终返回非零。
- 保存外部进程墙钟时间、编译阶段时间、产物字节数、源码字节数与吞吐率；统计最小值、中位数、均值、p95、最大值、标准差。
- `results.json` 保存逐次命令、编译和运行日志、超时信息、源码/二进制 SHA-256、编译器/链接器/同目录动态库 SHA-256、主机与 Python 信息。各样本源码及可执行文件保留在配置子目录。
- `report.md` 列出所有配置结果。前端计时来自两条既有流水线，阶段名称不同，分析时按各自阶段解释。

### 如何解读时间

编译时间包括驱动进程、前端、中间层、后端、目标文件输出和 polyld 链接。运行时间包含进程启动，短程序主要反映启动成本，不能作为数值计算吞吐性能。操作系统缓存保持自然状态，没有清空文件缓存；每次重新生成产物，没有增量构建缓存复用。

固定顺序逐配置执行，不与构建/测试并行。每配置只有 7 次正式测量，最近秩 p95 等于最大观测值，不能视为稳定尾延迟估计。结果是这台机器上的绝对基线，没有同环境的旧版正确产物或其他编译器对照，因此不声称加速倍数。

## 初版优化与边界

为避免已有优化在循环、调用和 SSA 上误编译，默认 `O2/O3` 不再运行尚未具备可靠 CFG/别名语义的实验性变换。

- O1：常量折叠、复制传播、死代码消除、CFG 规范化、冗余 phi 消除、基本块内公共子表达式消除。
- O2：在 O1 上增加强度削减及清理。
- O3：在 O2 上增加逃逸分析和再次清理。

实验性循环展开、向量化等实现仍留在代码中，但不作为默认编译性能承诺。ARM64 当前用固定栈槽保存 SSA 值，优先保证调用、递归和 phi 并行复制正确；该发射路径不使用 `--regalloc` 指定的实验性分配器。

**尚未完成的能力：** 完整标准库、动态语言对象运行时、Java/JVM 与 C#/CLR 生态、命令行参数入口、可变参数 ABI、超出 8 个整数或 8 个浮点寄存器参数的栈传参，以及 ARM64 预编译跨语言 bridge ABI。遇到这些后端限制会诊断失败，不以空指令替代。整数样例在有限范围内计算，不能据此证明 Python 任意精度整数或各语言所有溢出/异常语义。

x86_64、Linux、Windows、WebAssembly 仍保留已有代码路径；本报告的执行结论只覆盖 macOS ARM64。此次为新增 IR 常量同步了 x86/Wasm 发射支持，但没有把其他平台单元测试等同于跨平台运行验证。跨语言 IMPORT/LINK 与独立源文件是不同验证范围，本矩阵覆盖用户要求的各语言独立源文件。

## 本次结果

详见 [2026-10-02 完整结果摘要](benchmarks/native-2026-10-02.md)，以及本地 `build-release/native-evaluation/2026-10-02/results.json` 的原始样本。
