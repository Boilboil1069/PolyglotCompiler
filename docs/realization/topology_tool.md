# Topology Analysis Tool — Implementation Details

## 1. Architecture Overview

The topology analysis tool (`polytopo`) provides Simulink-style function I/O identification, link checking, and validation for `.ploy` cross-language programs. It models every function, constructor, method, pipeline, and cross-language call as a **TopologyNode** with typed input/output **Ports**. Connections between ports are represented as **TopologyEdges** with type-compatibility status.

The system consists of four core library modules, a CLI executable, and a Qt-based GUI panel integrated into the PolyglotCompiler IDE (`polyui`).

### 1.1 Component Map

```
tools/polytopo/
├── include/
│   ├── topology_graph.h       # Core data structures: Port, TopologyNode, TopologyEdge, TopologyGraph
│   ├── topology_analyzer.h    # AST-to-graph builder using PloySema for type resolution
│   ├── topology_validator.h   # Edge type validation, cycle detection, unconnected port warnings
│   └── topology_printer.h     # Output formatters: text, DOT (Graphviz), JSON, summary
└── src/
    ├── topology_graph.cpp     # Graph operations: add/query nodes & edges, topo-sort, cycle detection
    ├── topology_analyzer.cpp  # Two-pass AST walker: register nodes, then walk bodies for edges
    ├── topology_validator.cpp # Five validation passes: types, ports, params, cycles, lang compat
    ├── topology_printer.cpp   # Coloured text, DOT with record nodes, JSON, summary with statistics
    └── polytopo.cpp           # CLI entry point with argument parsing and 5-stage pipeline

tools/ui/common/
├── include/
│   └── topology_panel.h      # QGraphicsView-based topology visualization panel
└── src/
    └── topology_panel.cpp     # Interactive node rendering, edge drawing, validation overlay, export

tests/unit/tools/
└── topology_test.cpp          # Catch2 unit tests for graph, analyzer, validator, printer
```

### 1.2 Library Target

The `topo_lib` CMake library (`tools/CMakeLists.txt`) links against `polyglot_common` and `frontend_ploy`, providing:

- `TopologyGraph` — graph data structure with topological sort (Kahn's algorithm) and DFS cycle detection.
- `TopologyAnalyzer` — builds the graph from a parsed and semantically analyzed ploy AST.
- `TopologyValidator` — validates type compatibility, port connectivity, parameter counts, and language interop constraints.
- `TopologyPrinter` — renders the graph in multiple formats.

## 2. Core Data Structures

### 2.1 Port

Each port represents a typed parameter (input) or return value (output) on a function node:

| Field       | Type            | Description                                   |
|-------------|-----------------|-----------------------------------------------|
| `name`      | `std::string`   | Parameter or return-value name                |
| `direction` | `Direction`     | `kInput` or `kOutput`                         |
| `type`      | `core::Type`    | Semantic type from the ploy type system       |
| `language`  | `std::string`   | Owning language (e.g. `"cpp"`, `"python"`)    |
| `index`     | `int`           | Positional index within the node              |
| `id`        | `uint64_t`      | Unique identifier assigned at graph-build time|

### 2.2 TopologyNode

Represents a callable unit in the topology graph:

| Kind            | Description                                        |
|-----------------|----------------------------------------------------|
| `kFunction`     | Standalone FUNC declaration or LINK target         |
| `kConstructor`  | NEW(...) class instantiation                       |
| `kMethod`       | METHOD(...) call on an object                      |
| `kPipeline`     | PIPELINE block                                     |
| `kMapFunc`      | MAP_FUNC type conversion helper                    |
| `kExternalCall` | Cross-language CALL(lang, func, ...)               |

### 2.3 TopologyEdge

Connects an output port to an input port with compatibility status:

| Status            | Meaning                                          |
|-------------------|--------------------------------------------------|
| `kValid`          | Types are directly compatible                    |
| `kImplicitConvert`| Widening / numeric promotion required            |
| `kExplicitConvert`| Requires MAP_TYPE or explicit CONVERT            |
| `kIncompatible`   | Types are fundamentally incompatible — error     |
| `kUnknown`        | One or both types are `Any` / unresolved         |

## 3. Analysis Pipeline

### 3.1 TopologyAnalyzer (two-pass)

**Pass 1 — Register top-level nodes:**

For each top-level declaration in the module AST:
- `FuncDecl` → Create a `kFunction` node with input ports (parameters) and output ports (return type).
- `LinkDecl` → Create both a caller node and a callee node (marked as `is_linked`), plus an edge between them.
- `PipelineDecl` → Create a `kPipeline` node; recurse into the pipeline body to register contained functions.
- `MapFuncDecl` → Create a `kMapFunc` node.
- `ExtendDecl` → For each method in the extension, create a `kMethod` node.

**Pass 2 — Walk function bodies for edges:**

Within each function body, the analyzer resolves:
- `CrossLangCallExpr` → Creates a `kExternalCall` node (if not already present) and an edge from the caller to the callee.
- `NewExpr` → Creates a `kConstructor` node and corresponding edge.
- `MethodCallExpr` → Resolves the receiver and creates an edge.
- `CallExpr` → Resolves the callee function and creates an edge.
- `VarDecl` → Tracks data-flow bindings through variables (var → producer node + port).
- `Identifier` → Looks up variable bindings to propagate producer information.

### 3.2 TopologyValidator (five passes)

1. **Edge type validation** — Checks every edge's source and target port types for compatibility.
2. **Unconnected port detection** — Warns about input ports with no incoming edge and output ports with no outgoing edge.
3. **Parameter count validation** — Ensures callers provide the correct number of arguments.
4. **Cycle detection** — Uses DFS to detect and report circular dependencies.
5. **Language compatibility** — Flags known interoperability issues between specific language pairs.

### 3.3 Type Compatibility Rules

The validator uses a multi-level type compatibility check:

| Source Type → Target Type      | Status            |
|--------------------------------|-------------------|
| Same type                      | `kValid`          |
| Either is `Any`                | `kUnknown`        |
| Int → Float                    | `kImplicitConvert`|
| Bool → Int                     | `kImplicitConvert`|
| Same container, same element   | `kValid`          |
| Class with matching name       | `kValid`          |
| Otherwise                      | `kIncompatible`   |

## 4. CLI Tool — `polytopo`

### 4.1 Usage

```
polytopo [options] <file.ploy>

Options:
  --format <text|dot|json|summary>   Output format (default: text)
  --validate                         Run validation checks
  --strict                           Treat Any types as errors
  --no-color                         Disable ANSI colour codes
  --show-locations                   Include source locations in output
  --output <file>                    Write to file instead of stdout
  --compact                          Compact output format
  --allow-cycles                     Do not report cycles as errors
  --dot-horizontal                   Use left-to-right DOT layout
  --help                             Show usage
```

### 4.2 Pipeline

The CLI follows a 5-stage pipeline:

```
[1/5] Lex       ─ PloyLexer tokenizes the source
[2/5] Parse     ─ PloyParser builds the AST
[3/5] Sema      ─ PloySema performs semantic analysis
[4/5] Build     ─ TopologyAnalyzer constructs the graph
[5/5] Validate  ─ TopologyValidator checks the graph (optional)
```

### 4.3 Output Formats

- **text** — Box-drawn ASCII topology with coloured port names and types.
- **dot** — Graphviz DOT format with record-shaped nodes and port-level edges.
- **json** — Machine-readable JSON with full node/edge metadata.
- **summary** — Compact statistical overview (node counts, edge status distribution, language distribution).

## 5. GUI Panel — TopologyPanel

The `TopologyPanel` (integrated into `polyui` via `PanelManager`) provides:

- **Interactive graph visualization** using `QGraphicsView` / `QGraphicsScene`.
- **Node rendering** with colour-coded language badges, input/output port dots, and box-drawn labels.
- **Edge rendering** with cubic Bezier curves colour-coded by compatibility status.
- **Validation overlay** — nodes with errors are highlighted with a red border.
- **Export** — DOT, JSON, and PNG export from the toolbar.
- **Zoom/pan/fit** — standard viewport controls.
- **Layout modes** — top-down and left-to-right grid layouts.
- **Details panel** — `QTreeWidget` showing node properties when selected.
- **Diagnostics panel** — `QPlainTextEdit` showing validation messages.

### 5.1 Integration

The panel is registered in `MainWindow::SetupDockWidgets()` via `PanelManager`:

```cpp
panel_manager_->RegisterPanel("topology", topology_panel_, "Topology");
```

Toggle: **View → Toggle Topology Panel** (`Ctrl+Shift+T`).

## 6. Testing

Unit tests are in `tests/unit/tools/topology_test.cpp` and registered as the `test_topology` CTest target:

| Test Suite       | Coverage                                                |
|------------------|---------------------------------------------------------|
| `[topology][graph]`    | Node/edge CRUD, find-by-name, roots/leaves, topo-sort, cycle detect, language distribution |
| `[topology][validator]`| Valid edges pass, cycle detection raises error          |
| `[topology][printer]`  | Text/DOT/JSON/summary output format verification       |
| `[topology][analyzer]` | FUNC, LINK, PIPELINE, CALL end-to-end from .ploy source|

## 7. Code Generation — `GeneratePloySrc`

The `topology_codegen.h`/`.cpp` module (shared between the polyui IDE and the polytopo CLI) converts a `TopologyGraph` into valid `.ploy` source code.

### 7.1 Component Location

```
tools/polytopo/
├── include/
│   └── topology_codegen.h     # GeneratePloySrc() and ParseJsonToGraph() declarations
└── src/
    └── topology_codegen.cpp   # Full implementation of graph-to-source and JSON-to-graph
```

### 7.2 Code Generation Rules

| Node Kind       | Generated .ploy Construct                                   |
|-----------------|-------------------------------------------------------------|
| `kFunction`     | `FUNC name(inputs) -> output_type { body }`                |
| `kConstructor`  | `LET v = NEW(language, class, args);` inside caller body   |
| `kMethod`       | `LET v = METHOD(language, object, method, args);`          |
| `kPipeline`     | `PIPELINE name { FUNC run(...) { ... } }`                  |
| `kMapFunc`      | `MAP_FUNC name(inputs) -> output_type { body }`            |
| `kExternalCall` | Skipped (represented as CALL targets within function bodies)|

| Edge Scenario                        | Generated Construct                                    |
|--------------------------------------|--------------------------------------------------------|
| Different-language nodes             | `LINK(src_lang, tgt_lang, src_name, tgt_name) { ... }` |
| Type mismatch across languages       | `MAP_TYPE(lang::SrcType, lang::TgtType);`              |
| Same-language call                   | `LET v = CALL(language, target, args);` in body        |

Additional directives:
- `IMPORT lang::module;` — emitted for each distinct foreign-language module.
- `EXPORT name;` — emitted for all non-external, non-map ploy-native functions.

### 7.3 Verification

The generated code is always verified by running `PloyParser` + `PloySema` before saving. Parse or semantic errors are reported in the diagnostics panel (GUI) or stderr (CLI).

### 7.4 Shared Usage

- **polyui IDE**: Click **Generate .ploy** in the toolbar → `OnGeneratePloy()` → writes `<basename>_generated.ploy` and opens in editor.
- **polytopo CLI**: `polytopo generate <topo.json> -o <output.ploy>` → `ParseJsonToGraph()` → `GeneratePloySrc()`.

Both paths use the same `GeneratePloySrc()` function from `topo_lib`, ensuring consistent output.

## 8. Bidirectional Sync Protocol

The topology panel and the code editor maintain a bidirectional live-sync relationship:

### 8.1 Editor → Topology (File Watcher)

```
Editor saves .ploy file
    → QFileSystemWatcher detects change
    → 200 ms debounce timer fires
    → TopologyPanel::BuildGraphFromFile() rebuilds graph
    → Status bar shows "Reloaded"
```

This is a complete re-parse and rebuild — the topology graph is reconstructed from scratch each time. The file watcher is temporarily removed during panel-initiated writes to prevent reload loops.

### 8.2 Topology → Editor (Edge Sync)

```
User drags to create an edge
    → TopologyPanel::TryCreateEdge()
    → TopologyPanel::SyncEdgeToFile()
        - Appends "LINK src.port -> tgt.port" to .ploy file
        - Emits FileContentChanged(file, line)
    → MainWindow handler:
        - Reloads file content in editor
        - Moves cursor to the new line
        - Applies yellow highlight (fades after 2 seconds)

User right-clicks to delete an edge
    → TopologyPanel::RemoveEdge()
    → TopologyPanel::RemoveEdgeFromFile()
        - Removes matching LINK/CALL line from .ploy file
        - Emits FileContentChanged(file, removed_line)
    → MainWindow handler (same highlight flow)
```

### 8.3 Current Capability Boundaries

| Capability                                | Status      |
|-------------------------------------------|-------------|
| Parse `.ploy` → build topology graph      | ✅ Full     |
| Generate `.ploy` from topology graph      | ✅ Full     |
| Live reload on file change                | ✅ 200 ms   |
| Edge creation syncs LINK to file          | ✅ Append   |
| Edge deletion syncs removal from file     | ✅ Line-match removal |
| Node creation/deletion from topology panel| ❌ Not yet  |
| Rename refactoring from topology panel    | ❌ Not yet  |
| Undo/redo for edge operations             | ❌ Not yet  |
| Incremental re-parse (partial rebuild)    | ❌ Full rebuild only |

## 9. Advanced Interaction Features

### 9.1 Force-Directed Layout

The topology panel uses a Fruchterman–Reingold-style simulation:

- **Repulsion**: Coulomb's law between all node pairs (`kRepulsionStrength = 50000`).
- **Attraction**: Hooke's law along edges (`kAttractionStrength = 0.005`, `kIdealEdgeLength = 250`).
- **Damping**: Simulated annealing with `kDamping = 0.85`, decaying over 300 iterations.
- **Early termination**: Stops when maximum movement per tick < `kMinMovement = 0.5`.
- **Edge refresh**: `RefreshEdgePositions()` recalculates all Bezier paths each tick.

### 9.2 Interactive Edge Creation/Deletion

- **Create**: Drag from an output `TopoPortItem` to an input port. A temporary `QGraphicsLineItem` previews the connection. On release, validation checks (no self-loops, no duplicates, output→input directionality) are performed. The new `LINK` statement is appended to the `.ploy` file.
- **Delete**: Right-click an edge → "Delete Edge". The panel removes the edge from `edge_items_`, syncs the removal to the `.ploy` file, and emits `GraphModified()`.

### 9.3 Debug Execution Highlighting

- `HighlightDebugNode(file, line)` — matches by source file and line number, sets `debug_active_` on the nearest node.
- `HighlightExecutingNode(uint64_t node_id)` — direct node-id-based highlighting.
- **Pulse animation**: When `debug_active_` is set, a `QTimer` oscillates the border opacity between 40% and 100% at 20 fps, creating a bright yellow pulsing effect.
- `ClearExecutionHighlight()` — stops the animation and resets the highlight.

### 9.4 Port Hover Tooltips

Hovering over a `TopoPortItem` displays:
- Port name and direction (Input/Output)
- Type name
- Parent node name
- Language
- Connection status: `✔ Valid`, `⚠ Implicit Convert`, `✘ Incompatible`, `? Unknown`, or `Not connected`

## 10. Sub-Window Drill-Down

When an expandable container node (e.g. `kPipeline`) is double-clicked, the topology panel opens a **DrillDownWindow** — a dedicated popup sub-window that displays only the internal nodes and edges belonging to that container. This avoids cluttering the main canvas with deeply nested call graphs.

### 10.1 Architecture

`DrillDownWindow` is a standalone `QWidget` (not embedded in the main `QGraphicsScene`). Each instance owns:

| Component | Type | Purpose |
|-----------|------|---------|
| `scene_` | `QGraphicsScene*` | Dedicated scene for the container's internal nodes |
| `view_` | `TopoGraphicsView*` | Interactive view with zoom / pan / rubber-band selection |
| `breadcrumb_` | `BreadcrumbBar*` | Hierarchical path from root to current level |
| `details_tree_` | `QTreeWidget*` | Property inspector for the selected node |
| `diagnostics_output_` | `QPlainTextEdit*` | Validation messages scoped to this container |
| `node_items_` | `std::unordered_map<uint64_t, TopoNodeItem*>` | Cloned node items whose `context_node_id` matches the container |
| `edge_items_` | `std::vector<TopoEdgeItem*>` | Edges connecting the cloned nodes |
| `drill_down_windows_` | `std::unordered_map<uint64_t, DrillDownWindow*>` | Nested sub-windows for recursive drill-down |

The window title is set to `"Drill-Down: <container_name>"` and an independent status label shows the node/edge count.

### 10.2 Interaction Flow

```
User double-clicks an expandable node on the main canvas (TopologyPanel)
    → TopologyPanel::OpenDrillDownWindow(node_id)
        - If a window for this node_id is already open → raise + activateWindow
        - Else → create new DrillDownWindow(node_id, name, this, breadcrumb_path)
    → DrillDownWindow constructor:
        1. SetupUI()         — builds the splitter / view / details / diagnostics / breadcrumb
        2. PopulateScene()   — filters node_items_ and edge_items_ by context_node_id
        3. LayoutDrillDownNodes() — initial grid placement
        4. StartForceLayout()    — 300-iteration Fruchterman–Reingold simulation
    → Window is shown as a top-level popup
```

If the user double-clicks an expandable node **inside** a DrillDownWindow, the process recurses:

```
DrillDownWindow::OpenDrillDownWindow(nested_node_id)
    → Creates a child DrillDownWindow
    → Breadcrumb path is extended: [root, ..., current_container, nested_container]
```

### 10.3 BreadcrumbBar

The `BreadcrumbBar` widget provides hierarchical navigation between drill-down levels.

**Data Model:**

```cpp
struct BreadcrumbBar::Entry {
    QString label;           // Display text (e.g. "pipeline:data_flow")
    uint64_t node_id;        // Container node id (0 = root / main panel)
    QWidget *window;         // The window for this level (nullptr = root)
};
```

**Behaviour:**

- Each entry is rendered as a clickable `QPushButton`.
- Entries are separated by `"›"` arrow labels.
- Clicking a breadcrumb entry emits `EntryClicked(node_id, window)`, which raises and activates the target window.
- The path grows with each recursive drill-down and allows jumping back to any ancestor level.

### 10.4 Force-Directed Layout

DrillDownWindow reuses the same Fruchterman–Reingold simulation as the main panel (§9.1), with identical constants:

| Constant | Value | Purpose |
|----------|-------|---------|
| `kForceMaxIterations` | 300 | Maximum simulation steps |
| `kRepulsionStrength` | 50000 | Coulomb repulsion between node pairs |
| `kAttractionStrength` | 0.005 | Hooke attraction along edges |
| `kIdealEdgeLength` | 250 | Target edge length in pixels |
| `kDamping` | 0.85 | Simulated annealing decay factor |
| `kMinMovement` | 0.5 | Early termination threshold |

The simulation runs on a `QTimer` and calls `RefreshEdgePositions()` each tick to update Bezier edge paths.

### 10.5 Deduplication and Lifecycle

- **No duplicates:** `OpenDrillDownWindow()` checks `drill_down_windows_` before creating a new window. If a window for the same `container_node_id` already exists, it is raised.
- **Ownership:** Each `DrillDownWindow` is stored in the parent's `drill_down_windows_` map. When the parent is destroyed, all child windows are destroyed.
- **Signal forwarding:** `DrillDownWindow::NodeDoubleClicked(filename, line)` is connected to the parent panel so that navigation requests propagate up to `MainWindow` and open the corresponding source location in the editor.
