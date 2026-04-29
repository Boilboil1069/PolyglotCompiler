# 快速开始：分析多语言程序的性能

> 适用 v1.6.0+。对应需求 `2026-04-28-5`。

本教程对 `tests/samples/09_mixed_pipeline`（驱动 C++ 图像处理器和 Python
ML 模型的 Ploy 程序）进行性能分析。

## 1. 启用插桩构建

```powershell
polyc --profile-instrument `
      --emit=call-graph:build/mixed.cgjson `
      --emit=profile-symbols:build/mixed.symjson `
      tests\samples\09_mixed_pipeline\mixed_pipeline.ploy `
      -o build\mixed.exe
```

`--profile-instrument` 让中端在每个非桥接函数前后插入
`__ploy_rt_call_enter` / `__ploy_rt_call_exit` 钩子。在程序调用
`__ploy_rt_call_trace_enable(1)`（或 IDE 通过 `polyrt` 触发）之前，钩子始终
no-op。

## 2. 运行一次性 profile 会话

在 IDE 中按 `Ctrl+Alt+P` 打开 Profiler 面板。把 *Duration* 设为 2000 ms，
*Interval* 设为 200 ms，然后点击 **Run profile**。

幕后，IDE 调用：

```
polyrt profile --json <tmp>.json --duration-ms 2000 --interval-ms 200
```

运行结束后，Flame、Hotspots、Timeline 和 Languages 标签页都会被填充。
双击任意行可跳转到源代码位置。

## 3. 实时流式

要进行不限时长的会话，请点击 **Start stream** 而不是 **Run profile**。
IDE 会启动：

```
polyrt profile --stream <tmp>.ndjson
```

并跟读 NDJSON 输出。点击 **Stop stream** 终止。

## 4. 无 IDE / CI 用法

```powershell
polyrt profile --json profile.json --duration-ms 5000 build\mixed.exe
polyrt calltrace --json calltrace.json
```

把生成的 JSON 文件喂给你选择的仪表盘；schema 见
[profile_stream_schema_zh.md](../specs/profile_stream_schema_zh.md) 与
[call_graph_schema_zh.md](../specs/call_graph_schema_zh.md)。
