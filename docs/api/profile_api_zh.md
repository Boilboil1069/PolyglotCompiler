# 性能分析 / 调用图 API 参考

> 受众：工具作者、IDE 插件开发者、CI 集成者。
> v1.6.0 引入（需求 `2026-04-28-5`）。

## 运行时 C-ABI 钩子

声明位于 `runtime/include/services/call_trace.h`，由中端
`InstrumentCallTrace` Pass 在 `polyc --profile-instrument` 启用时插入。
两个指针都是编译器选择的稳定 id（通常是 `.rodata` 内的指针），运行时端使用
`==` 比较，而非 `strcmp`。

```c
void __ploy_rt_call_enter(const char *qualified_name, const char *language);
void __ploy_rt_call_exit (const char *qualified_name);
void __ploy_rt_call_trace_enable(int enabled);
int  __ploy_rt_call_trace_is_enabled(void);
```

当跟踪关闭时，实现会在第一个 relaxed 原子读取处短路，使 LTO 在生产构建中
死码裁剪这些调用。

## 运行时 C++ 接口

`polyglot::runtime::services::CallTracer`（单例，线程安全）：

* `Enter(name, language)` / `Exit(name)` — 嵌入者直调钩子。
* `DrainSnapshot()` — 原子交换，返回并清空缓冲区。
* `PeekSnapshot()` — 非破坏性读取。
* `Clear()` — 丢弃所有聚合状态。
* `static SerializeJson(const CallTraceSnapshot&)` — 生成
  `polyglot.calltrace.v1` 文档。

`polyglot::runtime::services::ProfileSink`：

* `static Open(path, stream_mode)` — 打开文件型 sink。
* `Push(const ProfileSample&)` — 追加一个样本（线程安全）。
* `Close()` — flush 并 join 写线程。
* `static SerializeSample(...)` — 生成单个 `polyglot.profile.v1` 样本对象。

## polyrt 命令行

* `polyrt bench --json <path>` — 一次性 benchmark 报告。
* `polyrt profile --json <path> --duration-ms <d> [--interval-ms <i>]`
  — 有界采样会话，输出包装文档。
* `polyrt profile --stream <path>` — 长期运行的 NDJSON 流式输出器。
* `polyrt calltrace --json <path>` — 排空当前 `CallTracer` 快照（测试常用）。

## polyc 命令行

* `polyc --emit=call-graph:<path>` — 写出 `polyglot.callgraph.v1` 文档。
* `polyc --emit=profile-symbols:<path>` — 并行写出
  `polyglot.profilesymbols.v1` 的 id ↔ 名称映射。
* `polyc --profile-instrument` — 为每个非桥接函数插入调用跟踪钩子
  （LTO 可裁剪）。

## IDE 端

`polyglot::tools::ui::ProfileSession`
（`tools/ui/common/include/profile_session.h`）暴露：

* `RunBenchmark()`、`RunProfile(...)`、`EmitAndLoadCallGraph(...)`、
  `StartProfileStream(...)`、`StopProfileStream()`。
* `LoadCallGraphJson(path)`、`LoadProfileJson(path)` — 加载已存在的文档
  （供 Open 对话框和集成测试使用）。
* 信号：`BenchmarkFinished`、`ProfileFinished`、`CallGraphLoaded`、
  `StreamSampleReceived`、`ToolErrorOutput`。
