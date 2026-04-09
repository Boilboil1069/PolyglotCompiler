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

## 7. Future Work

- Force-directed automatic layout (replacing the simple grid).
- Interactive edge creation/deletion via drag-and-drop.
- Live reloading when the source `.ploy` file changes.
- Breakpoint integration: highlight the currently executing node during debug sessions.
- Tooltip-based type detail inspection on hover over ports.
