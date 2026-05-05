# Compile Pipeline Inspector — IR Viewer / Diff + Asm Viewer + Source↔Asm

## Goal

Give users a transparent, Compiler-Explorer-style view of every
compilation: which stages ran, how long each took, what artefacts
they emitted, what the IR looks like before and after optimisation,
and how each instruction in the final disassembly maps back to the
source line that generated it.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `PipelineRun` | [`tools/ui/common/pipeline/pipeline_inspector.h`](../../tools/ui/common/pipeline/pipeline_inspector.h) | Loads `aux/pipeline.json`; six canonical stages with per-stage artefact list, total wall-clock time and a normalised duration histogram. |
| `IrModule` | [`tools/ui/common/pipeline/ir_viewer.h`](../../tools/ui/common/pipeline/ir_viewer.h) | Parses LLVM/MLIR-style IR text into foldable function and basic-block trees indexed by line number. |
| `DiffFunctions` | same | Line-level LCS diff between two `IrFunction` bodies; output enumerates `equal/added/removed` lines plus left/right line numbers for side-by-side rendering. |
| `LineBindingTable` | same | Three-way `source ↔ IR ↔ asset` binding; lookup from any axis returns the matching tuple. |
| `AsmModule` | [`tools/ui/common/pipeline/asm_viewer.h`](../../tools/ui/common/pipeline/asm_viewer.h) | Parses textual disassembly for x86_64, arm64 and wasm; resolves `.file`/`.loc` DWARF directives and inline polyasm `; src=file:line` hints; provides `AsmForSource` / `SourceForAsm` for the bidirectional jump. |

## Pipelines

* **Stage timeline.** polyc emits `aux/pipeline.json` after each
  build; `PipelineRun::LoadAux` populates the six stages, and
  `Histogram()` returns `(stage, ratio∈[0,1])` pairs normalised
  against the longest stage so the panel can draw a horizontal
  bar chart in one pass.
* **IR folding.** `IrModule::Parse` walks the dump line by line.
  `define <ret> @name(...)` and `func @name(...)` open a new
  function; identifier-prefixed `label:` lines open a basic block;
  the closing `}` closes the active block.  Every node carries its
  `start_line`/`end_line`, so the IDE can pin folds to source
  ranges.
* **IR diff.** `DiffFunctions(left, right)` flattens both function
  bodies into line vectors, runs the classic O(NM) LCS, and walks
  the table backwards to emit `equal/added/removed` records with
  left/right line numbers — ready for the standard side-by-side
  diff renderer.
* **Asm parsing.** `AsmModule::Parse` recognises three input
  conventions: DWARF `.file <id> "name"` + `.loc <id> <line>`
  directives establish the rolling `(file, line)` for subsequent
  instructions; inline `; src=<file>:<line>` (or `// src=...`)
  comments override that rolling state per instruction; bare
  identifier-prefixed `label:` lines mark function boundaries.  The
  parser is target-aware (x86_64, arm64, wasm) and stores the
  active target for downstream filtering.
* **Bidirectional jump.** `AsmModule::AsmForSource(file, line)`
  returns every instruction that originated from a source line, and
  `AsmModule::SourceForAsm(function, line)` returns the source
  position for a given asm line — together they back the synced
  cursors and hover-highlight in the asm viewer.

## Tests

* [`tests/unit/polyui/pipeline_inspector_test.cpp`](../../tests/unit/polyui/pipeline_inspector_test.cpp)
  validates stage-name round-trips, full `aux/pipeline.json` ingestion
  for all six stages, total-duration accumulation and histogram
  normalisation.
* [`tests/unit/polyui/ir_viewer_test.cpp`](../../tests/unit/polyui/ir_viewer_test.cpp)
  parses a multi-function IR dump (basic block boundaries excluded
  from the body), asserts the diff produces all three line kinds,
  and checks the three-way binding lookup.
* [`tests/unit/polyui/asm_viewer_test.cpp`](../../tests/unit/polyui/asm_viewer_test.cpp)
  exercises target round-trips, `.file`/`.loc`-driven binding,
  inline polyasm `; src=` hint binding, and the wasm target path.

The full polyui suite runs at 678 assertions across 149 cases.
