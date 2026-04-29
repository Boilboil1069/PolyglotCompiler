# Profile / Call-Graph API Reference

> Audience: tool authors, IDE plugin writers and CI integrators.
> Defined in v1.6.0 (demand `2026-04-28-5`).

## Runtime C-ABI hooks

Declared in `runtime/include/services/call_trace.h`.  Emitted by the
`InstrumentCallTrace` middle-end pass (enabled with
`polyc --profile-instrument`).  Both pointers are stable identifiers
chosen by the compiler — typically pointers into `.rodata` — so the
runtime side compares them with `==` rather than `strcmp`.

```c
void __ploy_rt_call_enter(const char *qualified_name, const char *language);
void __ploy_rt_call_exit (const char *qualified_name);
void __ploy_rt_call_trace_enable(int enabled);
int  __ploy_rt_call_trace_is_enabled(void);
```

When tracing is disabled the implementations short-circuit on the first
relaxed atomic load so LTO can dead-strip the calls in production builds.

## Runtime C++ surface

`polyglot::runtime::services::CallTracer` (singleton, thread-safe):

* `Enter(name, language)` / `Exit(name)` — direct hooks for embedders.
* `DrainSnapshot()` — atomic swap that returns and clears the buffer.
* `PeekSnapshot()` — non-destructive read.
* `Clear()` — drop all aggregate state.
* `static SerializeJson(const CallTraceSnapshot&)` — produces a
  `polyglot.calltrace.v1` document.

`polyglot::runtime::services::ProfileSink`:

* `static Open(path, stream_mode)` — opens a file-backed sink.
* `Push(const ProfileSample&)` — appends a sample (thread-safe).
* `Close()` — flushes and joins the writer thread.
* `static SerializeSample(...)` — produces one `polyglot.profile.v1`
  sample object.

## polyrt CLI

* `polyrt bench --json <path>` — single-shot benchmark report.
* `polyrt profile --json <path> --duration-ms <d> [--interval-ms <i>]`
  — bounded sampling session, writes a wrapper document.
* `polyrt profile --stream <path>` — long-running NDJSON emitter.
* `polyrt calltrace --json <path>` — drains the current `CallTracer`
  snapshot (useful in test fixtures).

## polyc CLI

* `polyc --emit=call-graph:<path>` — write a `polyglot.callgraph.v1`
  document next to the build output.
* `polyc --emit=profile-symbols:<path>` — write a parallel
  `polyglot.profilesymbols.v1` id ↔ name map.
* `polyc --profile-instrument` — insert call-trace hooks around every
  non-bridge function (LTO-removable).

## IDE side

`polyglot::tools::ui::ProfileSession` (`tools/ui/common/include/profile_session.h`)
exposes:

* `RunBenchmark()`, `RunProfile(...)`, `EmitAndLoadCallGraph(...)`,
  `StartProfileStream(...)`, `StopProfileStream()`.
* `LoadCallGraphJson(path)`, `LoadProfileJson(path)` — load pre-existing
  documents (used by the Open dialog and integration tests).
* Signals: `BenchmarkFinished`, `ProfileFinished`, `CallGraphLoaded`,
  `StreamSampleReceived`, `ToolErrorOutput`.
