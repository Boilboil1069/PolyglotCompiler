# 数据驱动的订单授权与履约引擎

这个示例是 `polyc` 的端到端业务验收项目：一个 `.poly` 入口、一条构建命令、四种本地语言前端、四个带对象模型的 vendored 包、运行时 CSV 输入，以及最终由本地 `polyld` 生成的单一原生可执行文件。

```text
data/orders.csv
       |
       v
Poly 文件读取、递归流处理、聚合与断言
       |
       +--> C++ order_policy：构造、字段状态、成员调用、析构
       +--> Python fraud_policy：构造、字段状态、成员调用、显式清理
       +--> Rust fulfillment_policy：结构体构造、&self/&mut self 方法、显式清理
       +--> Go logistics_policy：结构体构造、指针 receiver 修改、receiver 读取
       |
       v
8 行结果 + 分类统计 + checksum 483 -> 进程退出码 232
```

用户不需要分别运行 clang/GCC、CPython、rustc 或 Go，也不需要手工调用 `polyld`、添加 `-I`，或先运行包管理器。

## 四种语言的包解析与面向对象路径

`order_risk.poly` 用正式的包依赖语法声明四个项目内 vendored 包：

```poly
IMPORT cpp PACKAGE order_policy >= 1.0;
IMPORT python PACKAGE fraud_policy >= 1.0;
IMPORT rust PACKAGE fulfillment_policy >= 1.0;
IMPORT go PACKAGE logistics_policy >= 1.0;
```

Poly 前端解析依赖名与版本约束；随后 `polyc` 定位各包的 `poly.package.toml`，校验 `name`、`language` 和 manifest `version` 是否满足 `>=`/`<=`/`==`/`>`/`<`/`~=` 约束，并拒绝绝对路径、`..` 以及经符号链接越出项目 `packages/` 根目录的路径。`--no-package-index` 下缺包、重复匹配、错语言、源码包缺少 `source` 或版本不兼容都会硬失败；C++ 允许由 `include_dir` 声明 header-only 包。该 include 根会交给内置预处理器，因此 `cpp/pricing_engine.cpp` 可以直接写：

```cpp
#include <order_policy/pricing_session.hpp>
```

Python、Rust、Go manifest 的 `source` 则会被确定性地合并到对应 consumer 源码。合并后的同一源码既用于外语签名提取，也用于内置前端生成对象文件，避免“签名看见了包、真正编译却没看见”的分叉。Go 的 package 声明会在合并时规范化成 consumer 的 `package main`。整个过程不调用 pip、Cargo、Go modules 或网络包索引。

四条对象路径都参与每一行订单的真实结果交叉检查：

- C++ `OrderPricingSession`：四个 `int` 成员、双参数构造、成员读取/修改、多分支计算、析构清理；
- Python `FraudAssessment`：`__init__` 构造四字段状态、状态修改方法、风险等级方法、显式 `close()` 清理；
- Rust `FulfillmentSession`：命名字段构造、`&self` 业务方法、`&mut self` 状态清理方法；
- Go `LogisticsSession`：栈上结构体、`NewLogisticsSession` 构造函数、指针 receiver 修改与读取方法。

其中 `OrderPricingSession` 不是空壳类型。它实际执行：

- 带两个参数的构造函数，初始化价格、数量、商品小计与折扣状态；
- `subtotal()` 成员读取；
- `apply_discount()` 成员状态修改与多层业务分支；
- `payable()` 成员计算；
- `lifecycle_checksum()` 多字段读取；
- 析构函数中的四次成员清理写入。

这些对象均在各自语言模块内形成真实聚合状态并经 IR/backend lowering，而不是由 Poly 入口伪造结果。C++ 后端按 `0/4/8/12` 字节字段偏移生成真实 GEP、load/store 与地址计算。每种语言的对象 wrapper 都必须与同语言的自由函数规则给出相同结果，否则分别返回错误码 `60`–`63`，整条订单流水线立即失败。

## 数据读取与处理

可执行文件在运行时打开 `data/orders.csv`，而不是把八行数据编译成常量。`polyc` 只在程序引用文件 API 时注入仓库内置的轻量原生运行时：

- `file_open_ints(path)` 打开输入文件；
- `file_next_int(fd, eof)` 跳过 CSV 标题和分隔符，解析下一个有符号整数；
- `file_close(fd)` 关闭描述符。

`process_order_stream` 一直递归读取到真实 EOF，并通过普通原生 ABI 参数累计行数、决策 checksum 和四类业务结果。它不是八次硬编码的读取序列。测试还会复制 CSV，修改第一笔订单的数量、但保持预期决策不变，再运行同一个已编译二进制，并要求得到受控校验状态 `225`。这能证明真实业务输入确实进入已编译的定价、库存和物流计算。

当前文件运行时直接使用 x86_64 Linux/macOS 系统调用，不依赖 libc 或 CPython。

## 十六个跨语言业务调用

### C++：定价与对象一致性

- `pricing_subtotal`
- `pricing_discount`
- `pricing_payable`
- `pricing_session_payable`

### Python：欺诈特征与对象一致性

- `fraud_velocity_points`
- `fraud_amount_points`
- `fraud_risk_band`
- `fraud_session_band`

### Rust：库存、支付与对象一致性

- `inventory_reservable`
- `payment_authorization`
- `fulfillment_gate`
- `fulfillment_session_gate`

### Go：物流、决策与对象一致性

- `logistics_base_days`
- `logistics_capacity_delay`
- `logistics_decision`
- `logistics_session_decision`

这些函数包含嵌套分支、早返回、跨阶段数据依赖与四种最终业务状态。批准订单返回 `100 + 风险等级 × 10 + ETA`；审核、欺诈拒绝和库存拒绝分别返回 `20`、`40`、`30`。

## CSV 验收数据

| 订单 | 主要路径 | 决策 |
| ---: | --- | ---: |
| 1001 | 标准批准；区域 2 | 116 |
| 1002 | 小额批准 | 113 |
| 1003 | 中风险高金额，人工审核 | 20 |
| 1004 | 三次失败支付，欺诈拒绝 | 40 |
| 1005 | 扣除安全库存后数量不足 | 30 |
| 1006 | ETA 达到 10 天，SLA 审核 | 20 |
| 1007 | 对象定价路径与区域 1 批准 | 114 |
| 1008 | 可售库存为零 | 30 |

最终统计为 8 行、3 个批准、2 个审核、1 个欺诈拒绝、2 个库存拒绝，决策和为 `483`。成功退出码是 `483 - 251 = 232`。

## 一条构建命令

`run.sh` 中只有这一条编译命令：

```bash
../../build/polyc --strict --no-package-index -O0 \
  -o build/polyc/order_risk order_risk.poly
```

`--no-package-index` 禁止探测或调用宿主包管理器；四个包均由项目本地 manifest 确定性解析。四条源码 `IMPORT` 和四条 `PACKAGE` 声明驱动内置前端编译，十六个 `CALL` 驱动符号签名与跨语言链接，文件运行时按需注入，最后由同一构建目录中的本地 `polyld` 输出可执行文件。

## 文件

| 文件 | 职责 |
| --- | --- |
| `order_risk.poly` | 四个包依赖、CSV 流处理、十六个外语调用、统计与断言 |
| `packages/order_policy/poly.package.toml` | vendored 包元数据与 include 根声明 |
| `packages/order_policy/include/order_policy/pricing_session.hpp` | 四字段 C++ 类、构造/析构与成员方法 |
| `packages/fraud_policy/{poly.package.toml,src/fraud_policy.py}` | Python 包 manifest 与 `FraudAssessment` 类 |
| `packages/fulfillment_policy/{poly.package.toml,src/fulfillment_policy.rs}` | Rust 包 manifest 与 `FulfillmentSession` 类型 |
| `packages/logistics_policy/{poly.package.toml,src/logistics_policy.go}` | Go 包 manifest 与 `LogisticsSession` 类型 |
| `cpp/pricing_engine.cpp` | C++ 对象适配器与三个独立定价规则 |
| `python/fraud_engine.py` | Python 对象适配器与三个欺诈规则 |
| `rust/fulfillment_engine.rs` | Rust 对象适配器与三个库存/支付规则 |
| `go/logistics_engine.go` | Go 对象适配器与三个物流/决策规则 |
| `data/orders.csv` | 运行时业务输入与期望结果 |
| `run.sh` | 唯一构建命令、执行与退出码检查 |
| `test.sh` | 构建产物、符号、包解析和数据敏感性审计 |

## 运行

先在仓库根目录构建 `polyc` 与 `polyld`，然后：

```bash
cd examples/order_risk_analyzer
./run.sh
./test.sh
```

也可以通过 `POLYGLOT_BUILD_DIR=build-release ./test.sh` 选择仓库内其他构建目录。脚本会拒绝仓库外工具链，避免误用系统中的同名程序。

在受支持的 x86_64 POSIX 主机上，同一套验收也已注册到 CTest：

```bash
ctest --test-dir build --output-on-failure -R '^example_order_risk_analyzer$'
```

## 当前边界

跨语言 ABI 仍刻意使用确定性的整数标量；四种对象状态都留在所属语言模块内部，不跨语言传递对象句柄。C++ 具有确定性析构；当前静态子集中的 Python/Rust 使用显式 `close()`，Go 本身没有析构语义，因此示例使用构造与 receiver 生命周期，不把这些路径伪称为 GC、Rust `Drop` 或 Go 析构。这里验证的是项目内、manifest 驱动的单源码 header/source vendoring，不是对 pip/Cargo/Go modules 全生态或远程下载的兼容；当前每种语言使用一个 consumer 单元，多个同语言 consumer 共享同一源码包的独立对象化仍需后续模块模型，Go consumer 也暂不支持在合并后的包声明之外重新组织任意 import 块。原生文件运行时当前支持 x86_64 Linux/macOS；不支持的平台会明确拒绝，而不是退回系统编译器或解释器。

English: [README.md](README.md)
