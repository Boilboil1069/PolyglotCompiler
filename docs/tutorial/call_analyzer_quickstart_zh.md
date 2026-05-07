# 快速上手：浏览跨语言调用图

> **文档版本**：2.0.0  
> **更新日期**：2026-05-07  
> **项目**：PolyglotCompiler 1.45.2  
> **配套文件**：[call_analyzer_quickstart.md](call_analyzer_quickstart.md)

Call Analyzer 面板可视化 `polyc --emit=call-graph:<path>` 输出的静态调用图，并可叠加 Profiler 会话采集到的运行时调用次数。

## 1. 输出调用图

```sh
polyc --emit=call-graph:build/mixed.cgjson \
      tests/samples/09_mixed_pipeline/mixed_pipeline.ploy
```

```powershell
polyc --emit=call-graph:build\mixed.cgjson `
      tests\samples\09_mixed_pipeline\mixed_pipeline.ploy
```

输出 JSON 遵循 [polyglot.callgraph.v1](../specs/call_graph_schema.md)。

## 2. 打开 Call Analyzer

按 `Ctrl+Alt+G` 切换 Call Analyzer 停靠窗。点 **Load .cgjson** 选择 `build/mixed.cgjson`。

中央图视图按 BFS 最长路径分列布局；桥接桩用对比色绘制，使跨语言流量一目了然。

## 3. 检查调用方 / 被调方

在图或表格中点选一个节点，左栏会填充：

- `callers_tree_` — 调用所选节点的全部函数。
- `callees_tree_` — 所选节点调用的全部函数。

双击一行可在调用点打开源文件。

## 4. 按语言对过滤

右侧 `language_filter_list_` 列出已观察到的 `(from_language, to_language)` 对。切换某一行可隐藏匹配的边而无需重新加载文档。

## 5. 查找路径

在搜索输入框输入起点 / 终点 id，设置 **Max depth**，点击 **Find paths**。带环安全的有界 DFS 会把所有不超出深度预算的路径填入 `path_results_list_`。

## 6. 与 Profiler 联动

若 Profiler 面板在同一会话中加载了 profile，Call Analyzer 节点还会附带运行时调用次数 — 它们共享同一个 `ProfileSession` 与调用图模型实例。

## 7. 使用 `polytopo` 的 CLI 工作流

无头或 CI 场景，通过 `polytopo` 驱动同一模型：

```sh
polytopo build/mixed.cgjson                   # ASCII 摘要
polytopo --view-mode=dot  build/mixed.cgjson  # Graphviz DOT
polytopo --view-mode=json build/mixed.cgjson  # 机器可读
polytopo --filter-language=python build/mixed.cgjson
polytopo build/mixed.cgjson | dot -Tsvg > mixed.svg
```

## 参见

- [docs/specs/call_graph_schema.md](../specs/call_graph_schema.md) — Schema 参考。
- [docs/realization/call_analyzer_panel.md](../realization/call_analyzer_panel.md) — 设计说明。
- [profiling_quickstart_zh.md](profiling_quickstart_zh.md) — 采集 profile 并叠加调用次数。
