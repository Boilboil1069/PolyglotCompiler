# 快速上手：剖析多语言程序

> **文档版本**：2.0.0  
> **更新日期**：2026-05-07  
> **项目**：PolyglotCompiler 1.45.2  
> **配套文件**：[profiling_quickstart.md](profiling_quickstart.md)

本教程剖析 `tests/samples/09_mixed_pipeline`（一个驱动 C++ 图像处理器与 Python ML 模型的 `.ploy` 程序）。

## 1. 带插桩构建

```sh
polyc --profile-instrument \
      --emit=call-graph:build/mixed.cgjson \
      --emit=profile-symbols:build/mixed.symjson \
      tests/samples/09_mixed_pipeline/mixed_pipeline.ploy \
      -o build/mixed
```

```powershell
polyc --profile-instrument `
      --emit=call-graph:build\mixed.cgjson `
      --emit=profile-symbols:build\mixed.symjson `
      tests\samples\09_mixed_pipeline\mixed_pipeline.ploy `
      -o build\mixed.exe
```

`--profile-instrument` 通知中端在每个非桥接函数前后插入 `__ploy_rt_call_enter` / `__ploy_rt_call_exit` 钩子。钩子默认空操作，直到程序调用 `__ploy_rt_call_trace_enable(1)`（或 IDE 通过 `polyrt` 驱动开启）。

## 2. 运行一次性 profile 会话

在 IDE 中按 `Ctrl+Alt+P` 打开 Profiler 面板。**Duration** 设 2 000 ms、**Interval** 设 200 ms，点击 **Run profile**。

幕后 IDE 调用：

```
polyrt profile --json <tmp>.json --duration-ms 2000 --interval-ms 200
```

运行结束后 **Flame**、**Hotspots**、**Timeline**、**Languages** 标签会被填充。双击任意行跳转到源位置。

## 3. 实时流模式

点 **Start stream** 而非 **Run profile** 进行开放式会话。IDE 启动：

```
polyrt profile --stream <tmp>.ndjson
```

并追踪 NDJSON 输出。点 **Stop stream** 终止。

## 4. 无头 / CI 用法

```sh
polyrt profile   --json profile.json   --duration-ms 5000 build/mixed
polyrt calltrace --json calltrace.json
```

把生成的 JSON 喂入你选择的仪表盘；Schema 见 [profile_stream_schema.md](../specs/profile_stream_schema.md) 与 [call_graph_schema.md](../specs/call_graph_schema.md)。

## 5. 按语言细分

**Languages** 标签按宿主语言（`cpp`、`python`、`rust`、`java`、`dotnet`、`go`、`javascript`、`ruby`、`ploy`）汇总自时间。桥接时间归属到虚拟语言 **bridge**，使跨语言开销一目了然。

## 6. 与 Call Analyzer 联动

同一会话中按 `Ctrl+Alt+G` 打开 Call Analyzer 并加载 `build/mixed.cgjson`。每个节点现在带上来自当前 profile 的运行时调用次数 — 两个面板共享同一个 `ProfileSession` 与调用图模型实例。

## 参见

- [docs/specs/profile_stream_schema.md](../specs/profile_stream_schema.md) — NDJSON 流式 Schema。
- [docs/realization/profiler_panel.md](../realization/profiler_panel.md) — 设计说明。
- [call_analyzer_quickstart_zh.md](call_analyzer_quickstart_zh.md) — 运行时视图的静态对照面板。
