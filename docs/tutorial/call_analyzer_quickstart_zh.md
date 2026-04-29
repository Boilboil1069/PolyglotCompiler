# 快速开始：浏览跨语言调用图

> 适用 v1.6.0+。对应需求 `2026-04-28-5`。

调用分析器面板可视化 `polyc --emit=call-graph:<path>` 输出的静态调用图，
可选叠加来自 Profiler 会话的运行时调用计数。

## 1. 生成调用图

```powershell
polyc --emit=call-graph:build/mixed.cgjson `
      tests\samples\09_mixed_pipeline\mixed_pipeline.ploy
```

输出 JSON 遵循
[`polyglot.callgraph.v1`](../specs/call_graph_schema_zh.md)。

## 2. 打开 Call Analyzer

按 `Ctrl+Alt+G` 切换 Call Analyzer 停靠窗口。点击 **Load .cgjson** 并选择
`build/mixed.cgjson`。

中央图视图按 BFS 最长路径分列布局；桥接桩使用对比色绘制，便于发现跨语言
调用流量。

## 3. 查看 caller / callee

在图或表格中点选节点，左侧栏会填充：

* `callers_tree_` — 调用所选节点的所有函数。
* `callees_tree_` — 所选节点调用的所有函数。

双击行可打开对应源文件。

## 4. 按语言对过滤

右侧的 `language_filter_list_` 列出所有观察到的
`(from_language, to_language)` 对。切换行可在不重新加载文档的情况下隐藏对应
边。

## 5. 路径搜索

在搜索输入中输入起点 id 与终点 id，设置 **Max depth**，点击 **Find paths**。
有界 DFS（循环安全）会把所有深度内的路径填充到 `path_results_list_`。

## 6. 与 Profiler 联动

若同一会话中 Profiler 面板已加载 profile，Call Analyzer 的节点会额外显示
运行时调用计数 — 它们共享同一个 `ProfileSession` 与调用图模型实例。
