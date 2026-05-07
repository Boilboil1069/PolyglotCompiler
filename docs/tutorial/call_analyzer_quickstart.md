# Quickstart: Exploring a Cross-Language Call Graph

> **Document Version**: 2.0.0  
> **Last Updated**: 2026-05-07  
> **Project**: PolyglotCompiler 1.45.2  
> **Companion**: [call_analyzer_quickstart_zh.md](call_analyzer_quickstart_zh.md)

The Call Analyzer panel visualises the static call graph emitted by `polyc --emit=call-graph:<path>`, optionally overlaid with runtime call counts from a Profiler session.

## 1. Emit the call graph

```sh
polyc --emit=call-graph:build/mixed.cgjson \
      tests/samples/09_mixed_pipeline/mixed_pipeline.ploy
```

```powershell
polyc --emit=call-graph:build\mixed.cgjson `
      tests\samples\09_mixed_pipeline\mixed_pipeline.ploy
```

The output JSON conforms to [polyglot.callgraph.v1](../specs/call_graph_schema.md).

## 2. Open the Call Analyzer

Press `Ctrl+Alt+G` to toggle the Call Analyzer dock. Click **Load .cgjson** and select `build/mixed.cgjson`.

The graph view (centre) lays out functions in BFS-longest-path columns; bridge stubs are drawn in a contrasting colour so cross-language traffic stands out.

## 3. Inspect callers / callees

Click a node in the graph or the table. The left pane fills with:

- `callers_tree_` — every function that calls the selected node.
- `callees_tree_` — every function that the selected node calls.

Double-click a row to open its source file at the call site.

## 4. Filter by language pair

The right-hand `language_filter_list_` lists every observed `(from_language, to_language)` pair. Toggling a row hides matching edges without re-loading the document.

## 5. Find paths

Type a source id and a destination id in the search inputs, set **Max depth**, and click **Find paths**. The bounded DFS (cycle-safe) fills `path_results_list_` with every path that fits within the depth budget.

## 6. Combine with the Profiler

If the Profiler panel has loaded a profile during the same session, the Call Analyzer's nodes additionally show the runtime call counts — they share the same `ProfileSession` and call-graph model instance.

## 7. CLI workflow with `polytopo`

For headless or CI use, drive the same model through `polytopo`:

```sh
polytopo build/mixed.cgjson                   # ASCII summary
polytopo --view-mode=dot  build/mixed.cgjson  # Graphviz DOT
polytopo --view-mode=json build/mixed.cgjson  # machine-readable
polytopo --filter-language=python build/mixed.cgjson
polytopo build/mixed.cgjson | dot -Tsvg > mixed.svg
```

## See also

- [docs/specs/call_graph_schema.md](../specs/call_graph_schema.md) — schema reference.
- [docs/realization/call_analyzer_panel.md](../realization/call_analyzer_panel.md) — design notes.
- [profiling_quickstart.md](profiling_quickstart.md) — capture a profile and overlay call counts.
