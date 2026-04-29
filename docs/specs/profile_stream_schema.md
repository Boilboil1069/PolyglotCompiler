# Profile Stream JSON Schema (`polyglot.profile.v1`)

> Emitted by `runtime/src/services/profile_sink.cpp` via the static
> `ProfileSink::SerializeSample()` helper.  `polyrt profile` writes either
> a single document (with `samples[]` array) or NDJSON (one sample per
> line) depending on the `stream_mode` flag passed to `ProfileSink::Open`.

## Document mode

```json
{
  "schema": "polyglot.profile.v1",
  "samples": [
    {
      "function": "main",
      "language": "ploy",
      "thread": "T0",
      "timestamp_ns": 0,
      "window_ns": 200000000,
      "calls": 1,
      "is_bridge": false
    }
  ],
  "frames": [
    { "language": "ploy",  "stack": ["main"],         "inclusive_ns": 5000, "self_ns": 1000, "calls": 1 },
    { "language": "python","stack": ["main", "calc"], "inclusive_ns": 4000, "self_ns": 4000, "calls": 5 }
  ],
  "hotspots": [
    { "function": "calc", "language": "python", "calls": 5, "inclusive_ns": 4000 }
  ]
}
```

| Section | Consumer | Notes |
| --- | --- | --- |
| `samples[]` | `TimelineModel` | One sample per fixed wall-clock window. Each sample becomes a bar in the swimlane lane keyed by `thread`. |
| `frames[]` | `FlameTreeModel` | Aggregated by stack prefix.  Missing → flame view falls back to `hotspots`. |
| `hotspots[]` | `CallGraphModel::ApplyRuntimeCounts` overlay | Optional convenience aggregation. |

## Stream mode (NDJSON)

`polyrt profile --stream <path>` writes one JSON object per line.  Each
object is a single `samples[]` entry (NOT the wrapper document).  The IDE
side (`ProfileSession::HandleStreamLine`) appends to the timeline model
incrementally and refreshes the views at ≥5 Hz.

## Field reference (`samples[]`)

| Field | Type | Description |
| --- | --- | --- |
| `function` | string | Qualified name of the dominant frame in the window. |
| `language` | string | Language of the dominant frame. |
| `thread` | string | Logical thread / lane id. |
| `timestamp_ns` | uint64 | Window start, monotonic. |
| `window_ns` | uint64 | Window length. |
| `calls` | uint64 | Cumulative calls during the window. |
| `is_bridge` | bool | Cross-language marshalling event. |

## Field reference (`frames[]`)

| Field | Type | Description |
| --- | --- | --- |
| `language` | string | Frame's host language. |
| `stack` | string[] | Top-down qualified-name list (root first). |
| `inclusive_ns` | uint64 | Time spent in the leaf inclusive of children. |
| `self_ns` | uint64 | Time spent in the leaf only. |
| `calls` | uint64 | Number of times the leaf was entered. |

## Versioning

* Adding fields is a non-breaking change (`v1` continues).
* Removing or renaming fields requires `polyglot.profile.v2`.
