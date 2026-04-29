# 性能分析器面板（Profiler Panel）

> 状态：v1.6.0 起正式发布 — 需求 `2026-04-28-5`。

## 目的

性能分析器面板将 IDE 变成 PolyglotCompiler 运行时的实时观测面：它驱动
`polybench` / `polyrt` 子进程，把 `polyglot.profile.v1` JSON 样本流式回灌进
共享数据模型，并以五个视图展现结果。

| 标签页 | 内容 |
| --- | --- |
| Flame | 由 `frames[]` 调用栈聚合而成的包含时间树 |
| Hotspots | 来自 `hotspots[]` 的可排序函数耗时表 |
| Timeline | 每线程一条泳道的时间线，源自 `samples[]` |
| Languages | 按宿主语言聚合的耗时占比 |
| Log | 通过 `ProfileSession::ToolErrorOutput` 捕获的子进程 stderr |

## 架构

```
ProfilerPanel ───┐
                 ├── 共享 ──► ProfileSession
CallAnalyzerPanel ┘
                       ├── FlameTreeModel    (data_models/flame_node.h)
                       ├── CallGraphModel    (data_models/call_graph_model.h)
                       └── TimelineModel     (data_models/timeline_model.h)
```

`ProfileSession` 拥有三个 Qt 项目模型以及与外部工具通信的 QProcess。它默认解析
IDE 同目录下的 `polyrt`、`polybench`、`polyc` 可执行文件；也可以通过
SettingsService 三层配置中的
`profiler.polyrtPath` / `profiler.polybenchPath` / `profiler.polycPath` 显式
指定路径。

## 运行模式

* **一次性 benchmark** — `RunBenchmark()` 调用 `polybench --json <tmp>` 并刷新
  flame 与 timeline 模型。
* **一次性 profile** — `RunProfile(duration_ms, interval_ms)` 调用
  `polyrt profile --json <tmp> --duration-ms <d>`。
* **实时流式** — `StartProfileStream(interval_ms)` 启动
  `polyrt profile --stream <pipe>`，按 NDJSON 流逐行解析，并以 ≥5 Hz 刷新视图。

## 数据来源

* Flame 帧：`frames[].stack[]` — 自顶向下的函数全限定名列表。
* Hotspots：`hotspots[]` — `function`/`language`/`calls`/`inclusive_ns`。
* Timeline 事件：`samples[]` — `function`/`language`/`thread`/
  `timestamp_ns`/`window_ns`/`calls`/`is_bridge`。

完整 schema 见 [profile_stream_schema_zh.md](../specs/profile_stream_schema_zh.md)。

## 快捷键

* `Ctrl+Alt+P` 切换 Profiler 停靠窗口。
* 双击 flame 节点或 hotspot 行时会发出 `OpenFileRequested(file, line)`，
  `MainWindow` 据此打开文件标签并定位光标。
