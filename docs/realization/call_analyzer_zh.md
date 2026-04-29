# 调用分析器面板（Call Analyzer Panel）

> 状态：v1.6.0 起正式发布 — 需求 `2026-04-28-5`。

## 目的

调用分析器面板渲染 `polyc --emit=call-graph:<path>` 输出的静态调用图，并叠加
来自 `polyrt profile` 的运行时调用计数。它与性能分析器互补：后者展示时间，
本面板展示结构与跨语言调用流量。

## 布局

| 区域 | 控件 | 用途 |
| --- | --- | --- |
| 顶部工具栏 | `Load .cgjson` / `Compile current file` / 源路径 | 驱动加载 |
| 左侧 | `callers_tree_` + `callees_tree_`（两个 `QTreeWidget`） | 选中节点的直接邻居 |
| 中央 | `graph_view_`（`QGraphicsScene`）+ `nodes_view_`（`QTableView`） | 分层 DAG 布局 + 可排序节点列表 |
| 右侧 | `language_filter_list_` + 路径搜索输入 + `path_results_list_` | 语言对过滤 + 有界 DFS 路径搜索 |

## 布局算法

`RepaintGraph()` 会基于 `CallGraphModel` 暴露的邻接表执行 BFS 最长路径分层，
然后为每一层分配一列 X 坐标，并按插入顺序在该列内纵向排布节点。桥接桩节点
会通过 `FlameTreeModel::LanguageColor("bridge")` 着上对比色，让跨语言流量
一目了然。

## 路径搜索

`CallGraphModel::FindPaths(src, dst, max_depth)` 执行迭代式 DFS，通过
`on_stack` 集合避免循环，通过每帧的游标栈把堆占用控制在 `max_depth × |E|`。
结果是包含起止端点的完整 id 序列。

## 语言对过滤

面板维护一份持久化的 `QSet<QPair<lang, lang>>` 禁用集合。当用户切换
`language_filter_list_` 中的一行时，所有匹配 `(from_language, to_language)`
对的边（以及表格视图中相应被取消选择的节点）会被以灰色重绘，无需重新加载
调用图。

## 快捷键

* `Ctrl+Alt+G` 切换 Call Analyzer 停靠窗口。
* 双击节点行会发出 `OpenFileRequested(file, line)`，由 `MainWindow` 路由到
  标签页编辑器。

## 与 Profiler 共享

`MainWindow` 构造两个面板时会调用
`call_analyzer_panel_->SetSession(profiler_panel_->Session())`，让它们共享同一个
`ProfileSession`。因此，在 Profiler 面板中加载 profile 时，运行时调用计数也会
通过 `CallGraphModel::ApplyRuntimeCounts` 叠加到 Call Analyzer 的节点上。
