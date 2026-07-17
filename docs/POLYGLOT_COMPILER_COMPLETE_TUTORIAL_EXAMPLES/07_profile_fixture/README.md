# 07 Profile fixture — Chapters 24, 29, and 37 — `FIXTURE`

`current_profile.json` 与 `current_profile.ndjson` 使用当前 `ProfileSink` 的 nested `calls` shape。它们用于重现教材记录的 Runtime producer 与 UI flat consumer mismatch。

The JSON and NDJSON files use the current nested `calls` shape from `ProfileSink`. They reproduce the documented mismatch between the Runtime producer and the UI's flat consumer.

```sh
jq '.samples[0].calls.entries[0]' current_profile.json
jq -s '{schema:"polyglot.profile.v1",samples:.}' current_profile.ndjson
```

查询输出见 `expected_query.json`。Timestamp 是示例 monotonic value，不是 wall-clock epoch。

The query output is in `expected_query.json`. Its timestamp is a sample monotonic value, not a wall-clock epoch.

