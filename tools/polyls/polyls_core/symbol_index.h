/**
 * @file     symbol_index.h
 * @brief    Workspace symbol index for navigation features
 *
 * The index records symbol definitions, declarations, implementations,
 * type-of relationships and reference sites for `.ploy` documents and
 * for the host-language modules they import (`cpp`, `python`, `rust`,
 * `java`, `dotnet`).  It is consumed by the polyls navigation handlers
 * (`textDocument/definition`, `declaration`, `implementation`,
 * `typeDefinition`, `references`) and persisted to `.polyc-cache/` so
 * subsequent server start-ups can answer queries without re-parsing the
 * whole workspace.
 *
 * The implementation is parser-light: it relies on regex-free, single
 * pass scans tuned to the cross-language LINK / IMPORT / EXPORT
 * vocabulary that the IDE actually navigates.  Heavier semantic
 * resolution lives in `frontends/ploy/.../sema.cpp` and is intentionally
 * not duplicated here — the index is meant to stay responsive on every
 * keystroke.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace polyglot::polyls {

/// 0-based location inside a workspace file (LSP wire format).
struct SymbolLocation {
  std::string uri;
  std::uint32_t line{0};
  std::uint32_t character{0};
  std::uint32_t end_line{0};
  std::uint32_t end_character{0};

  bool operator==(const SymbolLocation &other) const noexcept {
    return uri == other.uri && line == other.line &&
           character == other.character && end_line == other.end_line &&
           end_character == other.end_character;
  }
};

/// Categorical kind of an indexed symbol.
enum class IndexEntryKind {
  kFunction,         ///< `.ploy` FUNC, host-language free function.
  kPipeline,         ///< `.ploy` PIPELINE block.
  kStruct,           ///< `.ploy` STRUCT or host-language class/struct.
  kVariable,         ///< `.ploy` LET/VAR top-level binding.
  kImport,           ///< `IMPORT lang::module` or `IMPORT lang PACKAGE pkg`.
  kLink,             ///< `.ploy` LINK declaration (the .ploy-side anchor).
  kForeignFunction,  ///< Host-language function discovered in the workspace.
  kForeignClass,     ///< Host-language class/struct discovered.
};

/// A single indexed entry.  An entry's `definition` is mandatory; the
/// other locations are optional and may be absent (e.g. host-language
/// classes without a separate declaration site).
struct IndexEntry {
  std::string name;            ///< Bare identifier.
  std::string qualified_name;  ///< e.g. "cpp::image_processor::enhance".
  std::string language;        ///< "ploy" / "cpp" / "python" / "rust" / "java" / "dotnet".
  IndexEntryKind kind{IndexEntryKind::kFunction};

  SymbolLocation definition;
  std::optional<SymbolLocation> declaration;
  std::optional<SymbolLocation> implementation;

  /// For LINK entries: the foreign-language target (e.g. "cpp",
  /// "image_processor::enhance") so navigation can hop to the host file.
  std::string link_target_language;
  std::string link_target_qualified;

  /// Optional type whose declaration `textDocument/typeDefinition`
  /// should jump to.  For `.ploy` LET bindings we record the type
  /// annotation text (when present); for FUNCs we record the return
  /// type.  Empty string when no type is known statically.
  std::string type_definition_name;

  /// Free-form signature for hover-style display (currently unused by
  /// navigation but kept for completeness so the index file stays
  /// self-describing).
  std::string signature;
};

/// A reference site (one occurrence of @c name in the workspace, other
/// than a definition).  Kept separately so a single rebuild can clear
/// just the references owned by a given URI.
struct ReferenceSite {
  std::string name;       ///< Bare identifier (matches IndexEntry::name).
  SymbolLocation location;
  bool is_definition{false};
};

/// The index.  Operations are thread-safe.
class SymbolIndex {
 public:
  SymbolIndex();
  ~SymbolIndex();

  SymbolIndex(const SymbolIndex &) = delete;
  SymbolIndex &operator=(const SymbolIndex &) = delete;

  /// (Re-)index a single document.  Pass an empty @p text to drop the
  /// document from the index.  @p language_id is the LSP-wire language
  /// id ("ploy", "cpp", "python", "rust", "java", "csharp" / "dotnet").
  /// Unknown language ids default to a no-op (so opening foreign files
  /// in the editor never corrupts the index).
  void IndexDocument(const std::string &uri, const std::string &language_id,
                     const std::string &text);

  /// Drop everything the workspace knows about @p uri.
  void RemoveDocument(const std::string &uri);

  /// All entries currently held (defensive copy).  Used by tests and by
  /// the cache writer.
  std::vector<IndexEntry> Entries() const;

  // ── Navigation queries (results are LSP-ready locations) ─────────────

  /// Definition sites for @p name (matches by bare identifier).
  std::vector<SymbolLocation> Definition(const std::string &name) const;

  /// Declaration sites — falls back to Definition() when no separate
  /// declaration was recorded.
  std::vector<SymbolLocation> Declaration(const std::string &name) const;

  /// Implementation sites — falls back to Definition() when no separate
  /// implementation was recorded.
  std::vector<SymbolLocation> Implementation(const std::string &name) const;

  /// `textDocument/typeDefinition`: the declaration of the type of the
  /// symbol named @p name (e.g. for `LET buf = NEW(cpp, ImageBuffer …)`
  /// we return the location of `cpp::image_processor::ImageBuffer`).
  std::vector<SymbolLocation> TypeDefinition(const std::string &name) const;

  /// `textDocument/references`.  When @p include_definition is true the
  /// definition site is included in the result list.
  std::vector<SymbolLocation> References(const std::string &name,
                                         bool include_definition) const;

  // ── Cross-language lookups ───────────────────────────────────────────

  /// Forward jump: given a `.ploy` LINK target descriptor, return the
  /// definition of the matching host-language symbol (if indexed).
  std::vector<SymbolLocation> CrossLanguageTarget(
      const std::string &target_language,
      const std::string &qualified_target) const;

  /// Reverse jump: given a host-language symbol, return every `.ploy`
  /// LINK site that targets it.  Used by `references` queries issued
  /// from inside a host-language file.
  std::vector<SymbolLocation> CrossLanguageBackrefs(
      const std::string &target_language,
      const std::string &qualified_target) const;

  // ── Persistence ──────────────────────────────────────────────────────

  /// Write the current snapshot to @c cache_dir/symbol_index.json.
  /// Creates @p cache_dir on demand.  Returns true on success.
  bool SaveToCache(const std::string &cache_dir) const;

  /// Read a previous snapshot from @c cache_dir/symbol_index.json,
  /// replacing the current contents.  Returns false when the cache file
  /// does not exist or is malformed (the index is left empty in that
  /// case).
  bool LoadFromCache(const std::string &cache_dir);

  /// Number of indexed documents (for diagnostics + tests).
  std::size_t DocumentCount() const;

  /// Number of indexed entries (for diagnostics + tests).
  std::size_t EntryCount() const;

 private:
  // Per-language indexers.
  void IndexPloy(const std::string &uri, const std::string &text);
  void IndexCpp(const std::string &uri, const std::string &text);
  void IndexPython(const std::string &uri, const std::string &text);
  void IndexRust(const std::string &uri, const std::string &text);
  void IndexJava(const std::string &uri, const std::string &text);
  void IndexDotnet(const std::string &uri, const std::string &text);

  void CollectReferences(const std::string &uri,
                         const std::string &text);

  // Mutex guards both maps.  All public APIs lock once.
  mutable std::mutex mu_;

  std::unordered_map<std::string, std::vector<IndexEntry>> entries_by_uri_;
  std::unordered_map<std::string, std::vector<ReferenceSite>> refs_by_uri_;
};

/// Decode a `file://` URI to a filesystem path.  Implemented here (not
/// in the server) so the cache writer can compose the cache directory
/// from the workspace root URI handed in via `initialize`.
std::string UriToPath(const std::string &uri);

/// Encode a filesystem path back into a `file://` URI.
std::string PathToUri(const std::string &path);

}  // namespace polyglot::polyls
