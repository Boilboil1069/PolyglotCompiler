# Performance Profiler Panel

> Status: GA in v1.6.0 — demand `2026-04-28-5`.

## Purpose

The Profiler panel turns the IDE into a real-time observability surface for
PolyglotCompiler runtimes.  It drives `polybench` / `polyrt` subprocesses,
streams `polyglot.profile.v1` JSON samples back into shared data models and
renders five views:

| Tab | Contents |
| --- | --- |
| Flame | Inclusive-time tree built from `frames[]` stack arrays |
| Hotspots | Sortable per-function table from `hotspots[]` |
| Timeline | Swimlane chart, one lane per thread, from `samples[]` |
| Languages | Per-language inclusive time breakdown |
| Log | Sub-process stderr captured via `ProfileSession::ToolErrorOutput` |

## Architecture

```
ProfilerPanel ───┐
                 ├── shares ──► ProfileSession
CallAnalyzerPanel ┘
                       ├── FlameTreeModel    (data_models/flame_node.h)
                       ├── CallGraphModel    (data_models/call_graph_model.h)
                       └── TimelineModel     (data_models/timeline_model.h)
```

`ProfileSession` owns all three Qt item models and the QProcess instances
that talk to the on-disk tools.  It auto-resolves `polyrt`, `polybench` and
`polyc` next to the IDE binary; explicit overrides come from
`profiler.polyrtPath` / `profiler.polybenchPath` / `profiler.polycPath`
keys in the SettingsService 3-layer config.

## Operation modes

* **One-shot benchmark** — `RunBenchmark()` invokes `polybench --json <tmp>`
  and re-populates the flame and timeline models.
* **One-shot profile** — `RunProfile(duration_ms, interval_ms)` invokes
  `polyrt profile --json <tmp> --duration-ms <d>`.
* **Live streaming** — `StartProfileStream(interval_ms)` spawns
  `polyrt profile --stream <pipe>` and parses NDJSON as it arrives,
  refreshing the views at ≥5 Hz.

## Data sources

* Flame frames: `frames[].stack[]` — ordered top-down list of qualified names.
* Hotspots: `hotspots[]` — `function`, `language`, `calls`, `inclusive_ns`.
* Timeline events: `samples[]` — `function`, `language`, `thread`,
  `timestamp_ns`, `window_ns`, `calls`, `is_bridge`.

The schemas are formally documented in
[profile_stream_schema.md](../specs/profile_stream_schema.md).

## Shortcuts

* `Ctrl+Alt+P` toggles the Profiler dock.
* Double-clicking a flame node or hotspot row emits `OpenFileRequested(file,
  line)`, which `MainWindow` routes to its tab editor and positions the
  cursor.
