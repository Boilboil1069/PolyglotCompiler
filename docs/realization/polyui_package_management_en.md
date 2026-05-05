# Package Management, Dependency Graph, Vulnerability Scan, REPL & Notebook

## Goal

Bring twelve language ecosystems under one IDE surface so that a
project can install, upgrade, remove and audit its dependencies
without leaving polyui — and let users explore mixed-language
projects through an embedded REPL and Notebook that share their
results across kernels.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `PackageManagerService` | [`tools/ui/common/packages/package_manager.h`](../../tools/ui/common/packages/package_manager.h) | Discovery + install / upgrade / remove + lockfile reads + `.ploy CONFIG` sync. Subprocess I/O is delegated to an injected `CommandExecutor`. |
| `PackageManagerRegistry` | same | Owns the twelve concrete backends and locates them by `Ecosystem` or by manifest filename. |
| `PipBackend` / `CondaBackend` / `UvBackend` / `PipenvBackend` / `PoetryBackend` | same | Python ecosystems. Parsers cover `requirements.txt`, `environment.yml`, `uv.lock` (TOML), `Pipfile.lock` (JSON) and `poetry.lock`. |
| `CargoBackend` / `NpmBackend` / `MavenBackend` / `GradleBackend` / `NugetBackend` / `GemBackend` / `GoModBackend` | same | Cargo, npm, Maven, Gradle, NuGet, Bundler and Go-Mod parsers. Each declares its manifest, its lockfile and the install / upgrade / remove argv. |
| `DependencyGraph` | [`tools/ui/common/packages/dependency_graph.h`](../../tools/ui/common/packages/dependency_graph.h) | Node + edge model, conflict detection, deterministic SVG export. `TreeView()` projects the graph from marked roots. |
| `VulnerabilityScanner` | [`tools/ui/common/packages/vulnerability_scanner.h`](../../tools/ui/common/packages/vulnerability_scanner.h) | Loads `Advisory` records via `ParseOsvDocument` / `ParseGitHubAdvisory`, matches versions through `VersionInRange`, supports per-id suppressions. |
| `ReplSession` | [`tools/ui/common/notebook/repl_session.h`](../../tools/ui/common/notebook/repl_session.h) | Long-lived engine wrapper. `DefaultSpec` ships argv / prompt / exit for `.ploy`, Python, IRust, IRB and dotnet-script. Actual I/O sits behind the pluggable `ReplTransport`. |
| `Notebook` | [`tools/ui/common/notebook/notebook.h`](../../tools/ui/common/notebook/notebook.h) | Cell list (code / markdown / cross-language link). `Execute` routes cells to sessions; `ToJson` / `LoadJson` round-trip the `.polynb` envelope. |

## Pipelines

* **Discovery and CONFIG sync.** `PackageManagerService::Discover`
  walks the workspace and any user-supplied candidate directories,
  matching files against each backend's `manifest_filename()`.  The
  resulting `Environment` records are activated one-per-ecosystem
  through `Activate`.  `SyncWithConfig` reconciles the resolved
  lockfile with the `.ploy CONFIG` requirement list, returning both
  `missing_in_lockfile` and `missing_in_config` so that the UI can
  highlight either drift direction.
* **Install / upgrade / remove.** Each backend builds the right
  argv (`pip install foo==1.2.3`, `cargo add foo`, `npm install
  foo@1.2.3`, etc.).  The service simply forwards them to the
  injected executor and surfaces the `CommandResult`.
* **Dependency graph and conflicts.** Lockfile reads populate a
  `DependencyGraph`.  `Conflicts()` groups nodes by name and reports
  any package resolved to more than one version.  `ExportSvg()`
  performs a BFS depth assignment from the marked roots, lays nodes
  out in deterministic columns and styles roots blue, conflict
  nodes red.
* **Advisory matching.** `VulnerabilityScanner::AddAdvisories`
  ingests `Advisory` lists parsed from osv.dev or the GitHub
  Advisory GraphQL feed.  `Scan` walks every `Package` and matches
  the package name (and ecosystem when both sides declare one)
  against every advisory's affected ranges.  Per-id suppressions
  silence accepted-risk findings.
* **REPL evaluation.** `ReplSession::Eval` records each input,
  stdout, stderr and error flag.  Transports may spawn the engine
  process or talk to an in-process kernel; the IDE only sees
  `ReplTurn` records.
* **Notebook execution.** `Notebook::Execute` dispatches code cells
  to the session bound to their `engine`.  `kCrossLanguageLink`
  cells evaluate the source-side expression first and the
  target-side expression second on two distinct sessions, then
  concatenate the outputs to record the whole `LINK` exchange.

## Tests

* [`tests/unit/polyui/package_manager_test.cpp`](../../tests/unit/polyui/package_manager_test.cpp)
  exercises the registry, every parser sample (pip, cargo, npm,
  Maven, go-sum), the executor routing and `SyncWithConfig`.
* [`tests/unit/polyui/dependency_graph_test.cpp`](../../tests/unit/polyui/dependency_graph_test.cpp)
  covers tree projection, conflict detection and SVG export.
* [`tests/unit/polyui/vulnerability_scanner_test.cpp`](../../tests/unit/polyui/vulnerability_scanner_test.cpp)
  validates range matching, both feed parsers, scan results and
  suppressions.
* [`tests/unit/polyui/notebook_test.cpp`](../../tests/unit/polyui/notebook_test.cpp)
  covers REPL spec defaults, transcript capture, cell move/remove,
  cross-language link execution and `.polynb` round-trips.

All four files run inside the polyui Catch2 binary; the suite
totals 545 polyui assertions across 128 cases.
