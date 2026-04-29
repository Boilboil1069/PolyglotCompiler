# Quickstart: Profiling a Mixed-Language Program

> Tutorial for v1.6.0+. Refers to demand `2026-04-28-5`.

This walk-through profiles `tests/samples/09_mixed_pipeline` (a Ploy
program that drives a C++ image processor and a Python ML model).

## 1. Build with instrumentation

```powershell
polyc --profile-instrument `
      --emit=call-graph:build/mixed.cgjson `
      --emit=profile-symbols:build/mixed.symjson `
      tests\samples\09_mixed_pipeline\mixed_pipeline.ploy `
      -o build\mixed.exe
```

The `--profile-instrument` flag tells the middle-end to insert
`__ploy_rt_call_enter` / `__ploy_rt_call_exit` hooks around every
non-bridge function.  The hooks are no-ops until the program calls
`__ploy_rt_call_trace_enable(1)` (or the IDE drives it through `polyrt`).

## 2. Run a one-shot profile session

In the IDE, press `Ctrl+Alt+P` to open the Profiler panel.  Set
*Duration* to 2000 ms and *Interval* to 200 ms, then click **Run profile**.

Behind the scenes the IDE invokes:

```
polyrt profile --json <tmp>.json --duration-ms 2000 --interval-ms 200
```

When the run finishes, the Flame, Hotspots, Timeline and Languages tabs
are populated.  Double-click any row to jump to the source location.

## 3. Live streaming

Click **Start stream** instead of **Run profile** for an open-ended
session.  The IDE spawns:

```
polyrt profile --stream <tmp>.ndjson
```

and tails the NDJSON output.  Click **Stop stream** to terminate.

## 4. Headless / CI usage

```powershell
polyrt profile --json profile.json --duration-ms 5000 build\mixed.exe
polyrt calltrace --json calltrace.json
```

Feed the resulting JSON files into your dashboard of choice; the schemas
are documented in
[profile_stream_schema.md](../specs/profile_stream_schema.md) and
[call_graph_schema.md](../specs/call_graph_schema.md).
