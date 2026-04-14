# Editor & Toolchain Expansion — Implementation Notes

> Realization document for demand **2026-04-14-1** (feature expansion).
> Covers all six sections of the demand entry.

---

## 1. polyui Editor Enhancements

### 1.1 Enhanced .ploy Completions

`CompilerService::GetPloyCompletions()` now returns a richer set of items:

- Built-in keywords: `FUNC`, `CALL`, `LINK`, `PIPELINE`, `EXPORT`, `IMPORT`, `VAR`, `RETURN`, `IF`, `ELSE`, `WHILE`, `FOR`, `STRUCT`, `ENUM`, `MATCH`, plus type names (`INT`, `FLOAT`, `STRING`, `BOOL`, `VOID`, `ANY`, `ARRAY`, `MAP`).
- Symbols extracted from opened files across the workspace via `IndexWorkspaceFile()`.

### 1.2 Workspace Symbol Indexing

`CompilerService::IndexWorkspaceFile()` performs regex-based multi-language symbol extraction:

| Language | Patterns |
|----------|----------|
| ploy | `FUNC`, `STRUCT`, `ENUM`, `VAR`, `PIPELINE` declarations |
| cpp | function definitions, `class`, `struct`, `enum`, `namespace` |
| python | `def`, `class` |
| rust | `fn`, `struct`, `enum`, `impl`, `mod` |
| java | `class`, `interface`, `enum` + methods |
| csharp | `class`, `struct`, `interface`, `enum` + methods |

### 1.3 Cross-File Go-to-Definition

`CompilerService::FindDefinition()` resolves identifiers to their declaration location using a three-tier search:

1. Current file symbols
2. Workspace-indexed symbols (all indexed files)
3. Regex fallback across all indexed source

Returns a `DefinitionLocation` with file, line, column.

### 1.4 "New From Template" Context Menu

Right-clicking a directory in the file browser shows **"New From Template..."**. The action presents a list of available templates (language-specific boilerplate), asks for a filename, writes the template to the selected directory, and opens the new file in the editor.

**Capability boundary:** Template content is static. Plugin-contributed templates (via `CAP_TEMPLATE`) are resolved by `PluginManager::GetTemplateProviders()` but not yet integrated into the template dialog.

---

## 2. Topology Tool Expansion

### 2.1 Node Grouping

The topology panel's toolbar includes a grouping combo box with four modes:

| Mode | Grouping key |
|------|-------------|
| None | No grouping |
| Language | `TopologyNode::language` |
| Pipeline | `TopologyNode::kind == "pipeline"` membership |
| Module | `TopologyNode::kind` |

Groups are visualised as translucent bounding rectangles with labels in the viewport.

### 2.2 Batch Operations

Three batch buttons operate on the current selection:

- **Delete**: removes selected nodes + connected edges from the scene, also removes them from the underlying `TopologyGraph` via `RemoveNodesIf` / `RemoveEdgesIf`.
- **Highlight**: starts a pulse animation on selected nodes.
- **Export**: builds a DOT subgraph of selected nodes and saves it to a file.

### 2.3 Source Location Comments in Generated .ploy

`GeneratePloySrc()` now emits `@source file:line` comments for LINK directives, PIPELINE node headers, and FUNC declarations when `SourceLoc` data is available.

### 2.4 polytopo CLI Filters

| Option | Description |
|--------|-------------|
| `--view-mode=<link\|call\|pipeline>` | Filter edges by type |
| `--filter-language=<lang>` | Filter nodes by language |

---

## 3. Compilation Experience & Observability

### 3.1 Progress JSON Events

`--progress=json` emits NDJSON events to stdout:

```json
{"event":"stage_start","stage":"frontend","index":0,"total":6}
{"event":"stage_end","stage":"frontend","index":0,"total":6,"ms":1.8}
{"event":"complete","success":true,"total_ms":27.7}
```

### 3.2 Build Profile

On successful compilation, `polyc` writes `build_profile.bin` to the aux directory. This binary file contains per-stage millisecond timings as 6 consecutive `double` values (frontend, semantic, marshal, bridge, backend, emit).

### 3.3 Error Summary

When a stage fails, `PrintErrorSummary()` aggregates all diagnostics by `ErrorCode` and prints a summary. In JSON mode, the summary is a JSON object keyed by error code.

### 3.4 Incremental Compilation Cache

`CompilationCache` (`compilation_cache.h`) provides a file-based hash cache using FNV-1a. The cache key is `ComputeHash(source_text, settings_key)` producing a 64-bit hash. Cached artefacts are stored in `.polyc_cache/` as `<hash>.cache` files.

| Command | Effect |
|---------|--------|
| `polyc --clean-cache` | Purges all cached entries and exits |

**Failure scenario:** If the cache directory is read-only, `Store()` silently fails. `HasCached()` returns false and compilation proceeds normally.

---

## 4. Plugin & Extension Ecosystem

### 4.1 New API Extension Points

Four new capability flags and provider structs:

| Flag | Provider struct | Exported symbol |
|------|----------------|-----------------|
| `CAP_COMPLETION` | `PolyglotCompletionProvider` | `polyglot_plugin_get_completion` |
| `CAP_DIAGNOSTIC` | `PolyglotDiagnosticProvider` | `polyglot_plugin_get_diagnostic` |
| `CAP_TEMPLATE` | `PolyglotTemplateProvider` | `polyglot_plugin_get_template` |
| `CAP_TOPOLOGY_PROC` | `PolyglotTopologyProcessor` | `polyglot_plugin_get_topology_proc` |

### 4.2 Version Constraints

`PolyglotPluginInfo::min_host_version` is checked at load time. `PluginManager::CheckHostVersionConstraint()` parses the SemVer string and compares against `POLYGLOT_VERSION_{MAJOR,MINOR,PATCH}`.

### 4.3 Conflict Detection

`PluginManager::DetectConflicts()` returns `PluginConflict` records for:

- Duplicate formatters for the same language
- Duplicate language providers for the same `language_name`

### 4.4 Sandbox & Circuit Breaker

`SandboxPolicy` controls:

- `call_timeout_ms` (default: 5000ms)
- `memory_limit_bytes` (default: 0 / unlimited)
- `max_consecutive_failures` (default: 3)

`RecordPluginFailure()` increments a per-plugin counter; when it reaches the threshold, `circuit_open` is set to `true`. `RecordPluginSuccess()` resets the counter. `ResetCircuitBreaker()` clears both state and error log.

### 4.5 Plugin Management UI

The Settings → Plugins page now includes:

- **Load Order** column in the plugin tree
- **Unload** button (fully removes the plugin from the process)
- **Reset Breaker** button (clears circuit breaker state)
- **Circuit breaker status** display (red "Breaker Open" in status column)
- **Last error** and breaker state shown in the details panel
- **Min Host Version** shown in plugin details

---

## 5. Testing

New end-to-end tests in `tests/unit/e2e/expansion_features_test.cpp`:

| Test | Assertion |
|------|-----------|
| Cache hash determinism | Same input → same hash |
| Cache hash divergence | Different input → different hash |
| Cache store/retrieve/clean | Round-trip correctness |
| New capability flags | Verify bit values and non-overlap |
| Provider struct zero-init | All fields are nullptr after `{}` init |
| Empty aggregation queries | No providers → empty results |
| Conflict detection (empty) | No plugins → no conflicts |
| Sandbox policy defaults | Verify default values |
| Sandbox policy round-trip | Set/get preserves values |
| Version header consistency | `POLYGLOT_VERSION_STRING` matches components |

All tests use behavioral assertions (REQUIRE / REQUIRE_FALSE); none use `REQUIRE(true)`.

---

## 6. Common Failure Scenarios & Troubleshooting

| Scenario | Symptom | Resolution |
|----------|---------|------------|
| Plugin requires newer host | Load rejected with "requires host >= X.Y.Z" message | Upgrade PolyglotCompiler or use an older plugin version |
| Two formatters for same language | `DetectConflicts()` returns a conflict | Disable one of the conflicting plugins |
| Plugin callback keeps crashing | Circuit breaker trips after 3 failures | Check `GetPluginLastError()`, fix the plugin, then "Reset Breaker" |
| Cache directory read-only | Cache store silently fails, no perf benefit | Fix directory permissions or set a different cache dir |
| `--progress=json` output mixed with stderr | JSON events go to stdout, progress text goes to stderr | Redirect stdout/stderr separately: `polyc ... 2>/dev/null` |
