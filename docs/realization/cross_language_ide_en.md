# Cross-language IDE — Navigation, Bridge Panel and Marshalling View

## Goal

Promote the cross-language story to a first-class IDE experience:
let users jump and rename across `.ploy` and host-language sources
through one coordinated edit, browse every generated bridge with
its live runtime call count, and inspect the conversion pipeline
that connects the two sides.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `LinkRegistry` | [`tools/ui/common/cross_language/cross_language_navigator.h`](../../tools/ui/common/cross_language/cross_language_navigator.h) | Catalogue of `.ploy` LINK sites + host-language definitions; goto-def, reverse references, CodeLens reference counts. |
| `RenamePlanner` | same | Builds a single coordinated `WorkspaceEdit` plan covering host-language definitions, every `.ploy` LINK site and the references discovered by the host-language LSP. |
| `BridgePanelModel` | [`tools/ui/common/cross_language/bridge_panel.h`](../../tools/ui/common/cross_language/bridge_panel.h) | Bridge inventory loaded from `aux/bridges.json`; tracks marshalling strategy, source location and live call count fed by polyrt calltrace. Re-import preserves runtime counts. |
| `MarshallingViewBuilder` | [`tools/ui/common/cross_language/marshalling_view.h`](../../tools/ui/common/cross_language/marshalling_view.h) | Per-bridge IR-lowering → helper → ABI-adapter chain. Loads `aux/marshalling.json` or synthesises the canonical pipeline from bridge metadata. |

## Pipelines

* **Goto definition.** polyls feeds the registry both the LINK sites
  parsed from `.ploy` and the host-language definitions resolved by
  the per-language LSP.  `GotoDefinition(site)` returns the matching
  definition in O(N) over the catalogue.
* **Reverse references / CodeLens.** `FindLinkReferences(def)` scans
  the LINK sites; `CodeLensFor(file)` yields per-definition lenses
  showing the count of `.ploy` references, anchored at the
  definition's source position.
* **Coordinated rename.** `RenamePlanner::Plan(language, symbol,
  new_name, extra_references)` walks every catalogued definition,
  every catalogued LINK site and every LSP-discovered reference,
  emitting one `WorkspaceEdit` per touched location.  polyls hands
  the resulting plan to the active LSPs as a single
  `workspace/applyEdit`.
* **Bridge inventory.** polyc emits `aux/bridges.json` after each
  build; `BridgePanelModel::ImportFromAux` ingests it and fills
  the panel.  `IncrementCallCount(id)` is wired to the polyrt
  calltrace stream so the panel updates live, and
  `FilterByLanguagePair` backs the column filters.
* **Marshalling chains.** `MarshallingViewBuilder::LoadAux` parses
  the per-symbol parameter/return chain emitted by polyc.  When a
  bridge has no recorded chain yet, `Synthesize(bridge)` builds the
  canonical three-stage pipeline (IR lowering, marshalling helper
  named after the strategy, ABI adapter for the target language)
  so the panel always has something to show.

## Tests

* [`tests/unit/polyui/cross_language_navigator_test.cpp`](../../tests/unit/polyui/cross_language_navigator_test.cpp)
  covers HostLanguage round-trips, goto-def, reverse references,
  CodeLens counts and coordinated rename plans (1 def + 2 sites + 1
  LSP reference → 4 edits).
* [`tests/unit/polyui/bridge_panel_test.cpp`](../../tests/unit/polyui/bridge_panel_test.cpp)
  parses an aux JSON document covering all five host languages and
  every marshalling strategy, asserts that re-imports preserve
  runtime counters, and exercises the language-pair filter helper.
* [`tests/unit/polyui/marshalling_view_test.cpp`](../../tests/unit/polyui/marshalling_view_test.cpp)
  parses a real chain document and synthesises chains for every
  one of the five host languages, verifying the three-stage layout
  and ABI-adapter labelling.

The full polyui suite runs at 626 assertions across 139 cases.
