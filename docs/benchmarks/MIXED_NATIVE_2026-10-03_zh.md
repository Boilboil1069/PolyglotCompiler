# 混合原生项目、性能与 UI 验收（2026-10-03）

本轮在 **macOS ARM64 本机**完成了真实订单应用的构建与执行、官方实现对照、三种输入规模的完整性能测量，以及 Qt 运行值显示验收。新增远程 CI 尚未执行，本文不把 workflow 配置视为远程通过证据。

## 1. 已验证的结果

| 项目 | 实际结果 |
| --- | --- |
| 混合订单工程 | O0–O3 × linear/graph，8 个配置 × 10 个输入场景，80/80 通过 |
| 四语言官方源码对照 | C++、Python、Rust、Go 原模块及对象规则，4 个成功输入场景全部一致 |
| 九语言独立源码 | 8 个 kernel × 9 种语言 × 8 个配置，576/576 原生执行通过 |
| 独立官方比较 | 512/512 配置一致；Poly 的另外 64 个配置只有数值 oracle |
| 正式混合性能实验 | 64/64 构建、832/832 原生执行校验通过；每配置 7 个有效测量样本 |
| Qt 完整工作区 | C++ 文档/定义、Python 文档/定义、真实运行追踪，3/3 gate 通过；含空格路径已实测 |
| 真实调用记录 | 128 个执行样本映射到 16 个编译器调用点；源码高亮、端口值和关闭覆盖恢复静态图均通过 |
| 额外回归 | 数值 ABI 8/8；最终 C++ 单测 47 cases / 407 assertions；Qt 图 40 / 561、追踪模型 5 / 52 |

混合应用每行输出 `ORDER <id> <decision>`，最终输出 `SUMMARY <rows> <approved> <review> <fraud> <inventory> <checksum>`。stdout 与结果文件逐字一致；正常退出码为 0。基准汇总为 `SUMMARY 8 3 2 1 2 483`。更改数量并更新预期能够成功运行；保留旧预期时必须以 225 拒绝。截断、空文件、非法数量、缺失文件、目录输出路径也核对了具体退出码与输出，崩溃不算正确拒绝。

各构建的 `polyglot.native-build.v1` 报告均包含 Poly/C++/Python/Rust/Go 五个实际模块、成功状态、对象路径与耗时；每个模块都继承了请求的优化级别和寄存器分配器。编译器及共享库哈希在测量前后保持一致。

## 2. 正式测量协议

- 主机：`macOS-27.0-arm64-arm-64bit`，10 个逻辑 CPU；驱动脚本 Python 3.9.6。硬件型号的 sysctl 读取受当前沙箱限制，未填写猜测值。
- 测量前结束本任务的其他构建、测试与 UI 进程。每轮按固定种子 `20261003` 打乱 8 个编译配置；不清空操作系统文件缓存。
- 每配置 1 次预热、7 次测量，共 64 次新构建。每次使用独立源码副本与产物目录；复制源码、官方参考构建不计入原生编译时间。
- 每个新产物先通过 10 个验收场景；第一次 8 行执行的延迟单独统计。随后独立启动 8、800、8000 行三个规模，按另一条固定种子随机流打乱顺序，不复用验收计时。
- 每次规模测量都是新进程，但使用已启动过的可执行文件。时间包含启动、输入读取、stdout 和完整结果文件写入；调用追踪关闭。
- 共 640 次验收执行 + 192 次独立规模执行 = 832 次执行。192 个规模观测中，24 个随预热构建排除，168 个进入统计。
- 大输入重复基准的业务规则分布并使用唯一订单 ID。独立整数 oracle 重新计算所有决策与汇总；800 行汇总为 `800 300 200 100 200 48300`，8000 行为 `8000 3000 2000 1000 2000 483000`。
- 官方原模块对照覆盖四个较小的成功场景。大规模数据检验处理量增长及完整输出，没有增加业务规则多样性。
- RSS 使用 `wait4.ru_maxrss` 的本次子进程峰值，不是同时运行的整个进程树内存之和。原始数据保留每条命令、输出、文件结果、编译元数据、版本与哈希。
- 所有样本保留。任一验收失败会使整个配置失去性能聚合。p95 使用 nearest-rank；只有 7 个测量值时，p95 等于这 7 个值的最大值。

### 先前一轮为何不用于正式规模比较

最初的 `2026-10-03-full` 复用了新产物第一次启动作为 8 行计时，而 800/8000 行在验收后运行。8 行中位数约 128–133 ms，明显混入首次启动状态差异。该轮完整原始数据保留，并以 `protocol-status.json` 标为 `diagnostic_only`；本文所有性能表只使用随后完整重跑的 `2026-10-03-full-balanced`。没有抽掉异常样本或只重跑较慢的配置。首次启动差异的具体系统原因没有单独分解。

## 3. 总编译与首次启动

单位为 ms；RSS 为 MiB。各项均为 7 个测量值的中位数。首次启动列独立于下一节规模比较。

| 优化 | 分配器 | 总编译 | 首次启动（8 行） | 编译峰值 RSS | 产物字节 |
| --- | --- | ---: | ---: | ---: | ---: |
| O0 | linear | 146.349 | 124.927 | 10.906 | 59856 |
| O0 | graph | 147.407 | 124.547 | 10.812 | 59856 |
| O1 | linear | 157.332 | 124.154 | 10.922 | 59856 |
| O1 | graph | 149.629 | 124.299 | 10.938 | 59856 |
| O2 | linear | 146.005 | 125.007 | 10.953 | 59856 |
| O2 | graph | 154.834 | 124.438 | 10.953 | 59856 |
| O3 | linear | 153.694 | 125.671 | 10.953 | 59856 |
| O3 | graph | 162.278 | 124.644 | 10.969 | 59856 |

## 4. 三种规模的独立运行

单位为 ms。每格为 **中位数 / p95**；各规模使用相同启动条件。

| 优化 | 分配器 | 8 行 | 800 行 | 8000 行 | 8000 行 RSS 中位数 MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| O0 | linear | 8.493 / 23.699 | 21.488 / 28.846 | 136.199 / 155.672 | 2.547 |
| O0 | graph | 8.884 / 11.309 | 23.113 / 30.167 | 131.306 / 147.887 | 3.406 |
| O1 | linear | 8.300 / 12.182 | 22.103 / 33.502 | 138.934 / 168.836 | 2.562 |
| O1 | graph | 8.232 / 12.464 | 22.711 / 26.858 | 142.365 / 166.573 | 3.422 |
| O2 | linear | 8.453 / 27.145 | 24.966 / 39.671 | 138.403 / 162.218 | 2.562 |
| O2 | graph | 7.837 / 9.107 | 23.539 / 29.564 | 141.905 / 209.126 | 3.422 |
| O3 | linear | 8.865 / 11.193 | 22.646 / 28.336 | 143.465 / 860.337 | 2.562 |
| O3 | graph | 8.098 / 9.960 | 22.832 / 36.489 | 147.424 / 158.262 | 3.422 |

O3 linear 的 8000 行包含一个 **860.337 ms** 长尾，已保留并进入 p95/均值/标准差。当前数据没有形成随优化级别单调加速的趋势，也不足以给优化器或分配器作稳定排名。这是一个包含大量文件和控制台 I/O 的应用测量；各语言模块承担不同业务，逐模块时间也不能用来做语言速度排名。

## 5. 逐模块编译

下表为编译器内部模块计时的中位数，单位 ms。模块之和不包含部分驱动、链接和运行库处理成本；各模块中位数相加也不等于总时间的中位数。

| 优化 | 分配器 | Poly | C++ | Python | Rust | Go |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| O0 | linear | 16.205 | 19.582 | 21.712 | 19.995 | 24.465 |
| O0 | graph | 16.104 | 18.968 | 22.109 | 24.348 | 22.376 |
| O1 | linear | 20.680 | 20.126 | 22.883 | 23.888 | 27.967 |
| O1 | graph | 22.816 | 21.259 | 20.698 | 24.406 | 25.605 |
| O2 | linear | 21.057 | 17.755 | 19.129 | 20.486 | 26.838 |
| O2 | graph | 22.159 | 21.850 | 23.004 | 20.697 | 28.092 |
| O3 | linear | 22.596 | 20.728 | 22.503 | 21.303 | 23.446 |
| O3 | graph | 22.917 | 20.523 | 20.610 | 21.748 | 26.822 |

## 6. 工具链与复现

| 官方实现 | 实际探测版本 |
| --- | --- |
| cpp | `Apple clang version 21.0.0 (clang-2100.3.34.2)` |
| python | `Python 3.9.6` |
| rust | `rustc 1.99.0 (b940084d7 2026-09-28)` |
| go | `go version go1.27.1 darwin/arm64` |
| java | `javac 21.0.8` |
| dotnet | `8.0.412 [/usr/local/share/dotnet/sdk]` |
| javascript | `v24.19.0` |
| ruby | `ruby 2.6.10p210 (2022-04-12 revision 67958) [universal.arm64e-darwin26]` |

编译器 SHA-256：`c6093f9977724821b2b717cf44216c8dc784be559def19a528ff0d627f162d4e`。全部共享库、应用源码、kernel 与测量脚本哈希，以及完整分布，保存在 [精简 JSON](MIXED_NATIVE_2026-10-03_summary.json)。

本地官方 Rust/Go 放在下列路径；其他机器可将 PATH 指向已安装的官方工具链。Node 参数也应使用本机实际安装路径。所有输出目录必须是新的。

```bash
export PATH="$PWD/build-release/reference-toolchains/rust/bin:$PWD/build-release/reference-toolchains/go/bin:$PATH"
python3 tests/native_programs/mixed_regression.py --polyc build-release/polyc \
  --output build-release/mixed-regression/new-run --require-reference
python3 scripts/differential_native.py --polyc build-release/polyc \
  --output build-release/native-differential/new-run --require-reference \
  --node /path/to/installed/node
python3 scripts/benchmark_mixed_native.py --polyc build-release/polyc \
  --output build-release/mixed-native-performance/new-run \
  --warmups 1 --repetitions 7 --row-counts 8 800 8000 --require-reference
python3 tests/native_programs/qt_smoke_gate.py \
  --polyui build-release/polyui.app/Contents/MacOS/polyui \
  --output "build-release/ui-validation/new run" --include-native-trace
```

原始证据（保留于本机工作区；精简 JSON 记录各 results.json 的 SHA-256）：

- `build-release/mixed-regression/2026-10-03-frozen/`：80 场景、参考构建与运行、模块继承记录。
- `build-release/native-differential/2026-10-03-frozen/`：576 原生配置、共享 kernel、官方参考和完整输出。
- `build-release/mixed-native-performance/2026-10-03-full-balanced/`：64 构建、832 执行、每次原始输出、顺序及正式统计。
- `build-release/ui-validation/2026-10-03/gate with spaces/`：C++/Python 文档定义导航截图、真实运行值截图及 3 项 gate JSON。

UI 操作和追踪范围见 [运行值与图关联说明](../UI_RUNTIME_TRACE_zh.md)。

## 7. CI 状态与范围

主 CI 已接入 `native-validation.yml`：显式安装官方语言工具链和 Qt，缺少工具、目标或输出不一致会失败；两平台运行真实原生程序与 Qt 工作区门禁。macOS ARM64 额外执行原生追踪及数值 ABI；Linux x86 明确记录原生追踪暂未实现，只验证追踪模型。CI 的单样本性能步骤只检验收集链路，不发布性能结论。

远程 workflow 未实际运行。本地先前 22 项 CTest 中，补全排序出现过一个失败；修复后最终定向 CTest 5/5 通过，包含该排序器及原生追踪。本文没有将那次旧 22 项运行改写成全通过。

全仓独立审计还存在既有差异：`docs/api/api_reference.md` 中英标题数 102/94 不一致；被忽略的旧 `tests/samples/01_basic_linking/1.json` 使用 `language=ploy`。这些不改变本轮执行证据，但本轮不据此声称整个仓库的所有 CI 和审计已经全绿。
