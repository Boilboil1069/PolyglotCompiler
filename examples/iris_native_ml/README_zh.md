# Iris 原生跨语言机器学习

这是一个由 PolyglotCompiler 完整编译、链接并运行的真实跨语言机器学习
项目。Poly 主程序流式读取 150 条鸢尾花记录，训练三种模型，执行两类统计
检验，持久化并重新加载模型，在分层留出集上评估，最后由原生程序写出 CSV
指标与完整 SVG 仪表盘。

构建和运行不会调用系统 C/C++ 编译器、CPython、Rust/Cargo、Go 工具链、
包管理器、Graphviz 或 libc 文件 API。`run.sh` 在调用 `polyc` 时把 `PATH`
清空，启用 `--no-package-index`，并显式指定仓库内的 `polyld`。

## 模型与结果

| 模型/检验 | 训练方式 | 留出集结果 |
|---|---|---|
| 三特征回归 | Q10 输入、Q100 权重、40 个 epoch、每批 12 条且类别均衡 | SAE 97、SSE 503、最大误差 10 Q10、R² 0.937536 |
| 深度为二的决策树 | 整数阈值网格搜索 | 30/30 正确 |
| 四特征最近质心 | 保存类别特征和，以无除法平方距离比较 | 29/30 正确 |
| Pearson 检验 | 精确整数中心矩 | r² 0.932867，`r² >= 0.90` 通过 |
| 单因素 ANOVA | 精确整数交叉乘法 | F 934.525，`F >= 100` 通过 |

回归目标是花瓣长度，特征为中心化后的萼片长度、萼片宽度和花瓣宽度。
最终参数为 `weights_q100 = [75, -64, 148]`、`bias_q10 = 38`。

量化训练 SAE 为 `350, 290, 278, 270, 266, 264, 267`。第七轮的小幅
回升来自真实的定点量化效应，并在第四十轮保持稳定；项目没有把损失伪造成
单调下降。

## 跨语言职责

- **Poly**：数据校验、流读取、epoch/batch 循环、网格搜索、聚合、持久化、
  重载校验、留出集评估和 SVG 生成。
- **Python**：数据契约和特征中心化，由内置 Python 前端 AOT 编译。
- **C++**：定点与统计矩内核；adapter 真实包含项目包中的
  `<iris_fixed/qmath.hpp>`。
- **Rust**：定点回归参数更新与误差函数。
- **Go**：决策树和最近质心预测。
- **外部包导入**：主文件声明 `IMPORT cpp PACKAGE iris_fixed >= 1.0`；包位于
  `packages/iris_fixed`，由项目 include 约定解析，不访问系统包索引或网络。

## 数据与定点协议

项目提交了 UCI 修正版 `bezdekIris.data`：150 条记录、4 个连续特征、3 个
类别、无缺失值。DOI、CC BY 4.0 署名、SHA-256 和转换规则见
[`data/README.md`](data/README.md)。

所有测量值采用 Q10，例如 `5.1 cm -> 51`。每个类别前 40 条用于训练，后
10 条用于测试，组成 120/30 的分层切分；同一 split 内按类别交错，因此每
个 12 条的训练 batch 恰好包含每类 4 条。最终程序只通过编译器注入的原始
系统调用运行时读取整数文本。

`tools/prepare_iris.py` 只是重建衍生数据的可选溯源工具；`run.sh` 和
`test.sh` 都不会调用它，它也不是项目运行依赖。

## 构建与验收

在仓库根目录运行：

```bash
cmake --build build --target polyc polyld -j2
examples/iris_native_ml/run.sh
examples/iris_native_ml/test.sh
```

成功时原生程序返回审计码 `73`，并生成：

- `artifacts/iris_model.pmodel`：带 schema、定点尺度和 checksum 的模型；
- `artifacts/metrics.csv`：回归、分类和统计检验指标；
- `artifacts/training_trace.csv`、`evaluation_trace.csv`：动态审计轨迹；
- `artifacts/iris_dashboard.svg`：150 点散点图、损失曲线、指标卡和混淆矩阵。

测试会连续两次构建同一个输出路径，同时覆盖 macOS 下 `polyld` 的签名 vnode
替换回归。模型格式详见 [`MODEL_FORMAT.md`](MODEL_FORMAT.md)：程序先写盘，
再关闭、重开、逐字段校验 18 个参数与 checksum，并且只使用重载后的参数做
留出集评估。

当前嵌入式文件运行时支持 x86_64 Linux 和 x86_64 Darwin。
