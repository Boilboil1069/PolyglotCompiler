# 四语言订单应用：原生执行与工程验收

`order_risk.poly` 是应用入口。一次 `polyc` 调用根据源码导入和项目内包 manifest，编译 C++ 定价、Python 风控、Rust 库存/支付、Go 物流，再由 `polyld` 链接成一个原生可执行文件。运行时读取订单文件，并逐笔输出决策和汇总。

编译应用不依赖 clang、CPython、rustc 或 Go。回归测试另外使用这些官方工具运行同一组外语源码，提供独立比较；这两条路径的结果在报告中分别记录。

## 构建与运行

先在仓库根目录构建 `polyc`、`polyld`。然后：

```bash
cd examples/order_risk_analyzer
POLYGLOT_BUILD_DIR=build-release ./run.sh
```

脚本只调用一次仓库内的 `polyc`。等价的核心命令如下：

```bash
../../build-release/polyc --strict --no-package-index -O0 --regalloc=linear \
  --build-report=build/polyc/build-report.json \
  -o build/polyc/order_risk order_risk.poly
build/polyc/order_risk data/orders.csv order-results.txt
```

`run.sh` 支持 `POLY_OPT_LEVEL=0..3`、`POLY_REGALLOC=linear|graph`，参数 1、2 分别传入输入和输出文件。默认使用 `data/orders.csv` 与 `order-results.txt`。成功退出码为 **0**。标准输出和结果文件逐字一致：

```text
ORDER 1001 116
ORDER 1002 113
ORDER 1003 20
ORDER 1004 40
ORDER 1005 30
ORDER 1006 20
ORDER 1007 114
ORDER 1008 30
SUMMARY 8 3 2 1 2 483
```

`SUMMARY` 后的六个整数依次是：订单数、批准数、人工审核数、欺诈拒绝数、库存拒绝数、决策校验和。行数和汇总在运行时计算，没有固定为 8 或 483。

## 输入与错误

输入列为：

```text
order_id,unit_price,quantity,recent_orders,failed_payments,available_units,delivery_zone,expected_decision
```

- 订单 ID 为正整数；单价范围 `1..1000000`，数量 `1..1000`。
- 近期订单数和库存范围 `0..1000000`，失败支付次数范围 `0..1000`，配送区域为 `1..3`。
- `expected_decision` 用于逐笔验收，必须与真实计算结果相符。
- 输入读取器是整数流读取器，会跳过标题文本与分隔符。它不是完整 CSV 语法解析器，不保证拒绝任意带引号、额外文字或错误分隔符的 CSV。
- 空输入、缺失字段、上述范围不合法、期望不匹配都会非零退出，且不会输出成功汇总。已经输出的有效订单行可能保留在结果文件中。

| 退出码 | 含义 |
| --- | --- |
| 0 | 全部订单通过，stdout 与结果文件写出成功 |
| 220 | 输入打开失败 |
| 221 | 文件关闭失败 |
| 222 | 空输入、截断记录或业务字段范围错误 |
| 225 | 实际决策与输入期望不符，包括对象路径一致性错误 |
| 226 | 无法归类的决策 |
| 227 | 结果文件打开或输出写入失败 |
| 228 | 汇总数组分配失败 |

## 四种语言的真实对象与包

入口保留 16 个跨语言业务调用，同时核对自由函数结果和对象方法结果。四个对象均由所属语言前端降低为真实聚合状态，留在自己的模块内，不跨语言传递对象句柄：

| 模块 | 包与对象路径 |
| --- | --- |
| C++ `pricing_engine.cpp` | `order_policy` 中 `OrderPricingSession`：四字段、构造、成员读写、析构 |
| Python `fraud_engine.py` | `fraud_policy` 中 `FraudAssessment`：构造、状态更新、风险分级、显式 `close()` |
| Rust `fulfillment_engine.rs` | `fulfillment_policy` 中 `FulfillmentSession`：结构体构造、`&self`、`&mut self` 方法 |
| Go `logistics_engine.go` | `logistics_policy` 中 `LogisticsSession`：结构体构造、指针 receiver 修改与读取 |

`IMPORT <language> PACKAGE <name> >= 1.0` 解析项目 `packages/` 下的 `poly.package.toml`。C++ 包声明 include 根；另外三个包的源文件合并到对应 consumer。`--no-package-index` 禁止宿主包管理器和网络索引查询。源码发现、签名提取和实际编译使用同一份包来源。

这验证项目内 manifest 驱动的 header/source vendoring，不声称兼容 pip/Cargo/Go modules 的完整生态。Python/Rust 的显式 `close()` 不代表 Python GC 或 Rust `Drop`；Go 不声明析构语义。

## 完整工程回归

在项目根目录运行：

```bash
python3 tests/native_programs/mixed_regression.py \
  --polyc build-release/polyc --output-root build-release/mixed-regression \
  --require-reference
```

也可以在示例目录执行 `./test.sh --require-reference`。脚本覆盖 **O0–O3 × linear/graph，共 8 个编译配置**。每个配置生成一次可执行文件，然后用同一个文件执行十个场景：

1. 原始八笔订单；
2. 第一笔数量由 7 改成 8，并更新期望，必须输出新结果 117；
3. 扩展到 24 笔订单，验证动态行数与汇总；
4. 倒序输入，验证逐笔顺序跟随输入；
5. 修改数量但保留旧期望，必须失败；
6. 截断记录；
7. 非法数量；
8. 空输入；
9. 缺少输入文件；
10. 输出目标为目录。

成功场景必须同时满足完整 stdout、完整结果文件、空 stderr 和退出码。错误场景核对指定的非零退出码与输出，不能只以“程序崩溃了”算通过。

每次构建还检查 `polyglot.native-build.v1` 报告：Poly 与四个外语模块必须都存在、成功、提供耗时与对象路径，并实际继承请求的优化等级和寄存器分配器。缺少元数据不是通过。

官方参考编译保留原始模块和包实现，只加标准入口/I/O 适配器；Python 测试驱动依次组合四个语言的真实计算结果，再与独立整数规则 oracle 及原生应用完整输出比较。缺少工具会记录为覆盖缺口；`--require-reference` 会让此情况硬失败。Poly 没有独立官方实现，因此这不构成 Poly 全语言一致性证明。

## 性能评估

先完成工程回归，在无其他编译任务的主机窗口执行：

```bash
python3 scripts/benchmark_mixed_native.py --polyc build-release/polyc \
  --output build-release/mixed-performance/run-001 \
  --warmups 1 --repetitions 7 --row-counts 8 800 8000 --require-reference
```

输出目录必须是新的，避免覆盖历史证据。每轮固定种子打乱八个配置的顺序；每次编译使用新源码副本和新产物目录。源码复制和官方参考构建不计入原生编译时间，不清空操作系统文件缓存。

默认共 64 次新编译：八个配置 ×（一次预热 + 七次测量）。首次启动延迟单独记录。完成十个验收场景后，每个产物独立处理 8、800、8000 行，并用固定种子打乱三个规模的顺序，不复用验收计时；每种规模均从已启动过的可执行文件创建新进程。大输入使用唯一订单 ID，重复相同业务规则分布，由独立整数规则重新计算每条决策与汇总；每种规模都逐字校验 stdout 和结果文件。官方源码对照覆盖四个较小的成功验收场景，大输入用于观察数据量增长，不增加业务规则覆盖面。

报告保存总编译时间、逐模块编译时间、各行数的运行时间分布、可用的峰值 RSS、二进制大小，以及全部原始测量、命令、版本与文件哈希。峰值 RSS 来自 `wait4` 的单次子进程资源记录，不是所有同时运行子进程内存之和。运行时间包括进程启动、读取、stdout 与文件写入。任一验收或规模运行失败，该配置不生成性能聚合。

`results.json` 保存原始证据，`report.md` 保存分布摘要。预热不进入统计；单次预检不能称为正式性能结果。

## CI 门禁和工具链

`.github/workflows/native-validation.yml` 由主 CI 调用，也可手动触发。Linux/macOS 作业显式安装 Go、Rust、Python、Node、Java、.NET、Ruby 和 Qt，运行原生差分、运行时、标准入口、真实混合项目和 Qt 工作区测试。macOS ARM64 还运行真实追踪与数值 ABI 回归，并检查 UI 中的运行值；Linux x86 只运行追踪模型测试，明确记录不支持原生追踪。缺少 Qt 目标、缺少官方参考、编译/运行/输出不一致均失败。CI 的单次性能脚本运行只验证测量链路，不发布性能结论。

Rust 使用 [官方 rustup 安装说明](https://rust-lang.org/tools/install/)，Go 使用 [官方安装说明](https://go.dev/doc/install)；本地探测版本和路径会写入报告。远程 CI 是否通过，只能以实际 workflow run 为依据，新增 workflow 文件不代表已经在远程运行。

English: [README.md](README.md)
