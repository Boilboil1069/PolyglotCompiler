# 完整项目示例

- [`iris_native_ml`](iris_native_ml/README_zh.md) — 定点鸢尾花机器学习项目：均衡批量回归、训练型决策树、最近质心分类、Pearson/ANOVA 检验、vendored 包导入、模型持久化与运行时 SVG；Python/C++/Rust/Go 均由 PolyglotCompiler 自带前端编译。
- [`house_price_ml`](house_price_ml/README_zh.md) — Python 生成数据、C++ 统计、Rust 训练、Go 评估的一元线性回归；只用仓库内置前端和链接器完成真实训练。
- [`customer_retention`](customer_retention/README_zh.md) — 可直接打开的零构建 HTML 客户留存仪表盘；Python 生成遥测、Java 计算活跃信号、JavaScript 计算健康分，业务源码均由本地 PolyglotCompiler 前端编译，原生程序生成页面数据，不使用外部语言编译器。
- [`order_risk_analyzer`](order_risk_analyzer/README_zh.md) — 一条 `polyc` 命令构建数据驱动的 Poly → C++ → Python → Rust → Go 订单引擎；覆盖 16 个跨语言调用、四种语言的对象状态/成员调用、四个本地 vendored 包、CSV 流读取和四类业务结果。
