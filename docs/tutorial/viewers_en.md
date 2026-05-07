# File-Type Viewers Tutorial

> **Document Version**: 2.0.0  
> **Last Updated**: 2026-05-07  
> **Project**: PolyglotCompiler 1.45.2  
> **Companion**: [viewers_zh.md](viewers_zh.md)

PolyUI ships dedicated viewers for images, hex / binary content
and SQLite databases.

## 1. Image viewer

Supported formats: PNG, JPEG, WebP, GIF, SVG, BMP. Format
detection looks at the leading magic bytes first and falls back
to the file extension for text-based formats.

* **Zoom**: mouse wheel or `+` / `-`. Clamped to 5 % – 6400 %.
* **Pan**: middle-drag or arrow keys.
* **Pixel pick**: click any pixel to read its RGBA value in the
  status bar.
* **Channel split**: cycle Red / Green / Blue / Alpha to inspect
  one channel at a time.

## 2. Hex viewer

Designed for very large files (≥ 1 GiB). The viewer never
materialises the whole file: it reads through a chunked I/O
callback and bounds the search overlap to `needle.size() - 1`
bytes so a match that straddles a chunk boundary still surfaces.

* **Jump**: `Ctrl+G` snaps to a row-aligned offset.
* **Find**: `Ctrl+F` searches a hex pattern from the cursor.
* **Highlight regions**: link analyses (e.g. `polyld` segment
  maps) push named highlights; the viewer renders them inline.

The IDE plugs schema renderers for known PolyglotCompiler
artifacts (`.ir.bin`, `.asm.bin`, `.obj`) so the right pane
shows decoded fields next to the raw bytes.

## 3. Binary inspector

`IdentifyBinary` reports the container kind (ELF / PE / Mach-O /
WASM), bitness, architecture (`x86_64`, `aarch64`, `wasm32`,
...), endianness and subsystem. The disassembly pane delegates
to `polyasm` when the architecture is supported and falls back
to a `.byte` placeholder render otherwise so the panel always
stays populated.

## 4. SQLite client and SQL console

The SQLite driver ships first (used by `samples/22_database_access`).

* **Schema browser**: lists tables and columns from the active
  connection.
* **Editor**: write SQL with completion driven by the schema
  browser. `Ctrl+Enter` runs the statement.
* **Result pager**: fixed-size pages, sortable headers,
  CSV export through `ExportCsv` (commas, quotes and newlines
  inside values are correctly escaped).
* **History**: every executed statement is recorded with its
  result; the buffer is bounded.

Other drivers (PostgreSQL, MySQL) plug into the same
`SqlDriver` interface.
