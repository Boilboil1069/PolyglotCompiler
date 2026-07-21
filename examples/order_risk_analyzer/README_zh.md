# 订单风控分析器：完整跨语言示例

这是一个可运行的小型业务项目，不是只有接口的框架。它读取订单 CSV，校验输入，逐单计算风险与运费，输出人类可读报告和结构化 JSON。

## 真实调用链

```text
orders.csv
  -> Python: CSV 解析、类型校验
  -> C++: 风险评分、运费计算
  -> Python: 决策分组、汇总、JSON 报告
```

Python 使用 `ctypes` 调用编译后的 C++ 动态库。边界不是模拟的：每条订单都会跨过 C ABI，测试还会把非法业务值传入 C++ 并验证错误码。

| 文件 | 职责 |
| --- | --- |
| `cpp/risk_kernel.h/.cpp` | 稳定 C ABI、评分规则、金额舍入 |
| `python/risk_bridge.py` | `ctypes.Structure` 类型映射和错误转换 |
| `python/report_pipeline.py` | CSV 校验、业务编排、汇总、JSON |
| `python/main.py` | 命令行入口 |
| `order_risk.poly` | 同一应用在 PolyglotCompiler 中的跨语言契约 |
| `tests/test_pipeline.py` | 正常数据、C++ 错误路径、坏 CSV 测试 |
| `expected_output.txt` | 端到端输出基准 |

## 运行

只需要 Python 3.10+ 和支持 C++17 的编译器，不依赖第三方包：

```bash
cd examples/order_risk_analyzer
./run.sh
```

运行后会生成 `build/librisk_kernel.*` 和 `build/report.json`。也可指定自己的 CSV 和 JSON 输出位置：

```bash
./run.sh data/orders.csv --json build/my-report.json
```

Windows 可使用 CMake 构建动态库，再运行 Python：

```powershell
cmake -S . -B cmake-build
cmake --build cmake-build --config Release
python python/main.py
```

## 测试

```bash
./test.sh
```

测试会重新构建 C++、执行完整数据流、逐字节比较 stdout 并运行 Python 单元测试。可另行执行 `../../build/polyc --no-package-index --check order_risk.poly` 检查 Poly 契约。

## Poly 编排说明

`order_risk.poly` 使用 `IMPORT`、`LINK`、`STRUCT` 和 `PIPELINE` 描述跨语言契约。当前版本的规范带签名 `LINK` 尚未把两个语言字段完整传给语义检查器，因此文件暂用带返回类型与映射的兼容形式，并在源码中标明迁移点。当前仓库的宿主语言前端也尚不能编译这里使用的完整标准 C++/Python 子集，所以可执行路径使用同 ABI 的 `ctypes` 桥；示例没有用固定 `PRINTLN` 冒充跨语言计算结果。等这两处能力完成后，可保持 C ABI 和业务文件不变，把入口切换到 Poly 原生编排。

English: [README.md](README.md)
