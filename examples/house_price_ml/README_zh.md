# 跨语言房价线性回归

这是一个会真实执行“生成数据 → 训练 → 留出集预测 → 评估”的小型机器学习项目。它不是固定输出的演示：编译后程序在运行时生成 12 条带噪声样本，用其中 8 条拟合一元线性回归，再用 4 条未参与训练的样本计算平均绝对误差（MAE）。

整个项目只有一条用户构建命令，并且只使用 PolyglotCompiler 仓库内构建出的 `polyc` 与 `polyld`：

```text
Poly       训练/测试切分、跨语言编排、结果断言
  ├─ Python  确定性生成面积特征、噪声与房价标签
  ├─ C++     计算 x² 与 xy 充分统计量
  ├─ Rust    使用普通最小二乘法拟合斜率与截距
  └─ Go      提供预测并计算留出集 MAE
```

不需要也不会运行 CPython、clang/GCC、rustc、Cargo 或系统 Go 编译器。

## 数据

Python 模块按下面的公式在编译后的程序中生成样本：

```text
x = 1 + row
noise = [+1, -1, -1, +1] 循环
y = 3x + 7 + noise
```

训练集中的噪声满足 `Σnoise = 0` 且 `Σ(x·noise) = 0`，因此模型可以从带噪声数据中恢复 `slope = 3` 与 `intercept = 7`。留出集四条样本各有 1 个价格单位的误差，所以 `MAE = 1`。

[`data/generated_samples.csv`](data/generated_samples.csv) 是便于人工查看的确定性数据快照。程序不会解析或硬编码该 CSV；同一批数据由 [`python/data_generator.py`](python/data_generator.py) 在编译后的跨语言执行路径中重新生成。

## 训练算法

Rust 模块根据 C++ 提供的充分统计量计算普通最小二乘解：

```text
slope     = (nΣxy - ΣxΣy) / (nΣx² - (Σx)²)
intercept = (Σy - slope·Σx) / n
```

本数据集得到：

```text
n=8, Σx=36, Σy=164, Σx²=204, Σxy=864
numerator=1008, denominator=336
slope=3, intercept=7
```

首版为了保持当前跨语言整数 ABI 完全确定，Rust 将正斜率量化到 `0..8`，截距量化到 `0..16`；Go 将 MAE 量化到 `0..8`。这是有明确业务域上界的整数模型，后续可以继续扩展为定点小数、更多特征和循环训练。

## 一条构建命令

[`run.sh`](run.sh) 实际执行的唯一构建命令等价于：

```bash
PATH=/nonexistent ../../build/polyc \
  --strict --no-package-index --quiet \
  --polyld=../../build/polyld -O0 \
  -o build/polyc/house_price_ml house_price_ml.poly
```

- `--no-package-index` 禁止进入 pip/npm/Cargo 等外部包索引路径。
- `--polyld` 显式绑定同一仓库构建目录中的链接器。
- 编译期间的 `PATH=/nonexistent` 会让任何意外的系统工具回退立即失败。
- 四个 `IMPORT` 由 `polyc` 自动发现，并递归使用自身内置前端生成四个本地对象。

## 运行和测试

先在仓库根目录构建 `polyc` 与 `polyld`，然后运行：

```bash
cd examples/house_price_ml
./run.sh
./test.sh
```

预期输出：

```text
house_price_ml: trained slope=3 intercept=7 holdout_mae=1 audit=142
```

二进制的 `main` 会先断言 OLS 分子、分母、模型参数和 MAE，再动态计算退出审计码 `3×40 + 7×3 + 1 = 142`。`test.sh` 还会验证四个前端对象、12 个外语描述符、最终二进制符号、唯一 `polyc` 命令和工具链隔离设置。

## 文件

| 文件 | 职责 |
| --- | --- |
| `house_price_ml.poly` | 数据切分、统计聚合、训练、预测、断言 |
| `python/data_generator.py` | 运行时合成带噪声数据 |
| `cpp/sufficient_statistics.cpp` | 平方与交叉乘积内核 |
| `rust/linear_regression.rs` | OLS 参数计算和有界整数商 |
| `go/model_service.go` | 模型预测、绝对误差与 MAE |
| `data/generated_samples.csv` | 人类可读的数据快照 |
| `run.sh` / `test.sh` | 原生构建、执行及完整性验证 |

English: [README.md](README.md)
