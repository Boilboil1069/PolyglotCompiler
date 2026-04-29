# Call Analyzer Panel

> Status: GA in v1.6.0 — demand `2026-04-28-5`.

## Purpose

The Call Analyzer panel renders the static + dynamic call graph emitted by
`polyc --emit=call-graph:<path>` and overlaid with runtime call counts from
`polyrt profile`.  It complements the Profiler panel: the latter shows
*time*, the analyzer shows *structure* and *cross-language traffic*.

## Layout

| Region | Widget | Purpose |
| --- | --- | --- |
| Top toolbar | `Load .cgjson` / `Compile current file` / source path | drive the loader |
| Left | `callers_tree_` + `callees_tree_` (`QTreeWidget` × 2) | direct neighbours of the selected node |
| Centre | `graph_view_` (`QGraphicsScene`) + `nodes_view_` (`QTableView`) | layered DAG layout + sortable node list |
| Right | `language_filter_list_` + path search inputs + `path_results_list_` | language-pair filter + bounded DFS path search |

## Layout algorithm

The `RepaintGraph()` helper does a BFS longest-path layering of the
adjacency map exposed by `CallGraphModel`, then assigns one X column per
layer and packs nodes vertically by insertion order.  Bridge stub nodes get
a contrast colour from `FlameTreeModel::LanguageColor("bridge")` so cross-
language traffic stands out at a glance.

## Path search

`CallGraphModel::FindPaths(src, dst, max_depth)` performs an iterative DFS
with an `on_stack` set to avoid cycles and a per-frame cursor stack so the
heap usage is bounded by `max_depth × |E|`.  Results are full id sequences
inclusive of both endpoints.

## Language-pair filter

The panel keeps a persistent `QSet<QPair<lang, lang>>` of disabled edges.
When the user toggles a row in `language_filter_list_`, all edges with a
matching `(from_language, to_language)` pair (and the unselected node hits
in the table view) are repainted greyed-out without re-loading the graph.

## Shortcuts

* `Ctrl+Alt+G` toggles the Call Analyzer dock.
* Double-clicking a node row emits `OpenFileRequested(file, line)`, routed
  by `MainWindow` to the tabbed editor.

## Sharing with the Profiler

`MainWindow` constructs both panels and calls
`call_analyzer_panel_->SetSession(profiler_panel_->Session())` so they
share a single `ProfileSession`.  Loading a profile in the Profiler panel
therefore also overlays runtime call counts onto the Call Analyzer's nodes
via `CallGraphModel::ApplyRuntimeCounts`.
