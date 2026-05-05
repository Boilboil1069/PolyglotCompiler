# Sample / Tutorial Browser + Topology Live + Inlay Hints

## Goal

Round out the IDE with three discoverability features: a curated
catalogue of in-tree samples and tutorials that opens as a
workspace copy, a topology view that follows the editor's focus
and updates incrementally, and inlay hints that surface inferred
types and parameter names directly in the editor.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `SampleCatalogue` | [`tools/ui/common/samples/sample_browser.h`](../../tools/ui/common/samples/sample_browser.h) | Loads a JSON catalogue of `tests/samples/` and `docs/tutorial/` entries, filters by language / topic / difficulty / free-text, plans a transactional copy of an entry into a destination directory. |
| `TopologyGraph` | [`tools/ui/common/topology_live/topology_live.h`](../../tools/ui/common/topology_live/topology_live.h) | In-memory graph (nodes + edges) with a `Neighbourhood(id, radius)` BFS that follows incoming and outgoing edges in lockstep. |
| `LiveTopologyTracker` | same | Focus-following view: `FocusOn(file, symbol, radius)` returns the symbol's neighbourhood (or any node anchored at `file` when the symbol is unknown), `NotifyEdit/ShouldRebuild` provide a debounced rebuild trigger, `NodeSource` resolves a clicked node back to source. |
| `InlayHintProvider` | [`tools/ui/common/inlay_hints/inlay_hints.h`](../../tools/ui/common/inlay_hints/inlay_hints.h) | Produces type hints (`: HANDLE<...>`) and parameter-name hints (`x:`) from analyser-supplied declarations and call arguments; both families are independently toggleable. |

## Pipelines

* **Sample catalogue.** polyc and the IDE share a `samples.json`
  index document that lists every entry's id, title, kind
  (`sample` / `tutorial`), difficulty, language tags, topic tags,
  source root and file list.  `SampleCatalogue::LoadIndex` ingests
  the document; `Filter` evaluates every populated criterion
  (language / topic membership, difficulty equality, kind equality,
  case-insensitive text match against title / id / summary) and
  returns the matching entries.  `PlanCopy(id, dst)` builds a
  `(src, dst)` list for every file under the entry root so the IDE
  can perform the copy transactionally.
* **Topology Live.** The static topology panel keeps the full
  graph; `LiveTopologyTracker::FocusOn` extracts the relevant
  neighbourhood whenever the editor reports a new active symbol,
  with a fallback to file-anchored nodes when the symbol is unknown
  (e.g. inside a non-symbol comment).  Edits are batched: the
  editor calls `NotifyEdit(now)` on every keystroke; the next
  `ShouldRebuild(now)` returns true once the configured debounce
  has elapsed since the last edit, at which point the orchestrator
  calls `ReplaceGraph(new_graph)` to install the incremental
  rebuild.  `NodeSource(id)` powers the topology→editor jump.
* **Inlay hints.**  polyls' analyser feeds the provider two
  vectors: declarations (each with the position right after the
  identifier and an inferred type string) and call arguments
  (each with the formal parameter name and the position where the
  argument expression starts).  `Produce` walks both, emits hints
  in declaration-then-argument order, sets `padding_left` on type
  hints and `padding_right` on parameter hints, and skips entries
  with empty type / parameter-name strings.  Settings flip the two
  families independently — clearing both yields no hints.

## Tests

* [`tests/unit/polyui/sample_browser_test.cpp`](../../tests/unit/polyui/sample_browser_test.cpp)
  validates kind / difficulty round-trips, JSON ingestion of mixed
  sample / tutorial entries, every filter axis (language, topic,
  difficulty, kind, free-text) and the per-file copy plan.
* [`tests/unit/polyui/topology_live_test.cpp`](../../tests/unit/polyui/topology_live_test.cpp)
  asserts that `Neighbourhood` follows both edge directions,
  `FocusOn` falls back to file-anchored nodes when the symbol is
  unknown, debouncing fires only after the quiet interval and
  consumes the pending state, and `NodeSource` returns the source
  position or nullopt for an unknown id.
* [`tests/unit/polyui/inlay_hints_test.cpp`](../../tests/unit/polyui/inlay_hints_test.cpp)
  exercises kind round-trips, the default both-families output,
  the four settings combinations and the empty-input skip rules.

The full polyui suite runs at 737 assertions across 162 cases.
