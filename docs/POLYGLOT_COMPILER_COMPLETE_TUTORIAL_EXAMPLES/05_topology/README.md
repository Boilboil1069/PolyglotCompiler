# 05 Topology — Chapters 14 and 25 — `RUNNABLE`

三个 `STAGE` 是 pipeline body 的语法 marker，但当前 analyzer 把整个 `PIPELINE audit` 折叠为一个节点，并不为各 stage 建边。`expected_summary.txt` 和 `expected.json` 保存了这个真实结果，因而同时说明工具能力边界。

The three `STAGE` declarations are pipeline-body syntax markers, but the current analyser collapses `PIPELINE audit` to one node and creates no per-stage edges. `expected_summary.txt` and `expected.json` preserve this real result and therefore document the tool boundary.

```sh
build/polytopo docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/05_topology/pipeline.poly --format summary
build/polytopo docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/05_topology/pipeline.poly --format json
```
