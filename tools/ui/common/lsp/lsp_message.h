/**
 * @file     lsp_message.h
 * @brief    Language Server Protocol (LSP) message types
 *
 * Pure C++ data types corresponding to the LSP 3.17 wire format.  All
 * types are JSON-serializable through the free helpers @ref ToJson and
 * @ref FromJson, which use nlohmann::json under the hood.  The types are
 * intentionally Qt-free so that polyls (the headless language server)
 * and the loopback unit tests can link against them without the GUI
 * stack.
 *
 * Coverage: types listed in demand 2026-04-28-19 §1 — Position, Range,
 * Diagnostic, Location, Hover, CompletionItem, SignatureHelp,
 * SymbolInformation, CodeAction, TextEdit, WorkspaceEdit,
 * DocumentSymbol — plus the lifecycle / textDocument message envelopes
 * required by §2 (initialize / shutdown / didOpen / didChange / didClose
 * / didSave / publishDiagnostics).
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::lsp {

using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// Severity / kind enums (numeric values match the LSP 3.17 wire format).
// ---------------------------------------------------------------------------

/// LSP DiagnosticSeverity (1=Error, 2=Warning, 3=Information, 4=Hint).
enum class DiagnosticSeverity : int {
  kError = 1,
  kWarning = 2,
  kInformation = 3,
  kHint = 4,
};

/// LSP CompletionItemKind (subset; values match the wire numbers).
enum class CompletionItemKind : int {
  kText = 1,
  kMethod = 2,
  kFunction = 3,
  kConstructor = 4,
  kField = 5,
  kVariable = 6,
  kClass = 7,
  kInterface = 8,
  kModule = 9,
  kProperty = 10,
  kKeyword = 14,
  kSnippet = 15,
  kStruct = 22,
};

/// LSP SymbolKind (subset; values match the wire numbers).
enum class SymbolKind : int {
  kFile = 1,
  kModule = 2,
  kNamespace = 3,
  kPackage = 4,
  kClass = 5,
  kMethod = 6,
  kProperty = 7,
  kField = 8,
  kFunction = 12,
  kVariable = 13,
  kConstant = 14,
  kStruct = 23,
};

// ---------------------------------------------------------------------------
// Geometry types
// ---------------------------------------------------------------------------

struct Position {
  std::uint32_t line{0};       ///< 0-based line number.
  std::uint32_t character{0};  ///< 0-based UTF-16 code unit offset within the line.

  bool operator==(const Position &other) const noexcept {
    return line == other.line && character == other.character;
  }
  bool operator<(const Position &other) const noexcept {
    return line < other.line || (line == other.line && character < other.character);
  }
};

struct Range {
  Position start;
  Position end;

  bool operator==(const Range &other) const noexcept {
    return start == other.start && end == other.end;
  }
};

struct Location {
  std::string uri;
  Range range;
};

// ---------------------------------------------------------------------------
// Diagnostic
// ---------------------------------------------------------------------------

struct Diagnostic {
  Range range;
  DiagnosticSeverity severity{DiagnosticSeverity::kError};
  std::string code;     ///< Free-form error code (e.g. "E1001"); may be empty.
  std::string source;   ///< Producer name (e.g. "polyls").
  std::string message;
};

// ---------------------------------------------------------------------------
// Text-edit primitives
// ---------------------------------------------------------------------------

struct TextEdit {
  Range range;
  std::string new_text;
};

struct WorkspaceEdit {
  /// Changes keyed by document URI.
  std::map<std::string, std::vector<TextEdit>> changes;
};

// ---------------------------------------------------------------------------
// Hover / Completion / Signature help
// ---------------------------------------------------------------------------

struct MarkupContent {
  std::string kind{"markdown"};  ///< "plaintext" or "markdown".
  std::string value;
};

struct Hover {
  MarkupContent contents;
  std::optional<Range> range;
};

struct CompletionItem {
  std::string label;
  CompletionItemKind kind{CompletionItemKind::kText};
  std::string detail;
  std::string documentation;
  std::string insert_text;
};

struct ParameterInformation {
  std::string label;
  std::string documentation;
};

struct SignatureInformation {
  std::string label;
  std::string documentation;
  std::vector<ParameterInformation> parameters;
};

struct SignatureHelp {
  std::vector<SignatureInformation> signatures;
  std::optional<int> active_signature;
  std::optional<int> active_parameter;
};

// ---------------------------------------------------------------------------
// Symbols / code actions
// ---------------------------------------------------------------------------

struct SymbolInformation {
  std::string name;
  SymbolKind kind{SymbolKind::kVariable};
  Location location;
  std::string container_name;
};

struct DocumentSymbol {
  std::string name;
  std::string detail;
  SymbolKind kind{SymbolKind::kVariable};
  Range range;
  Range selection_range;
  std::vector<DocumentSymbol> children;
};

struct CodeAction {
  std::string title;
  std::string kind;            ///< e.g. "refactor.extract.function".
  WorkspaceEdit edit;          ///< Optional inline edit; empty when N/A.
};

// ---------------------------------------------------------------------------
// Document sync
// ---------------------------------------------------------------------------

struct TextDocumentIdentifier {
  std::string uri;
};

struct VersionedTextDocumentIdentifier {
  std::string uri;
  std::int32_t version{0};
};

struct TextDocumentItem {
  std::string uri;
  std::string language_id;
  std::int32_t version{0};
  std::string text;
};

struct TextDocumentContentChangeEvent {
  /// When @ref range is std::nullopt the change replaces the entire
  /// document; otherwise it is an incremental change covering @ref range.
  std::optional<Range> range;
  std::string text;
};

struct DidOpenParams {
  TextDocumentItem text_document;
};

struct DidChangeParams {
  VersionedTextDocumentIdentifier text_document;
  std::vector<TextDocumentContentChangeEvent> content_changes;
};

struct DidCloseParams {
  TextDocumentIdentifier text_document;
};

struct DidSaveParams {
  TextDocumentIdentifier text_document;
  std::optional<std::string> text;
};

struct PublishDiagnosticsParams {
  std::string uri;
  std::vector<Diagnostic> diagnostics;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

struct ClientCapabilities {
  /// Free-form capability map.  The fields actually negotiated by polyls
  /// are read directly from the JSON payload by the server, so we keep
  /// this as a raw blob to forward verbatim.
  Json raw{Json::object()};
};

struct ServerCapabilities {
  /// 1 = full text sync (the only mode polyls supports for now).
  int text_document_sync{1};
  bool hover_provider{false};
  bool completion_provider{false};
  bool signature_help_provider{false};
  bool definition_provider{false};
  bool references_provider{false};
  bool document_symbol_provider{false};
  bool rename_provider{false};
  bool code_action_provider{false};
  bool diagnostic_provider{true};
};

struct InitializeParams {
  std::optional<int> process_id;
  std::optional<std::string> root_uri;
  ClientCapabilities capabilities;
  Json initialization_options{Json::object()};
};

struct InitializeResult {
  ServerCapabilities capabilities;
  std::string server_name{"polyls"};
  std::string server_version;
};

// ---------------------------------------------------------------------------
// JSON serialization (ToJson / FromJson) — explicit free functions.  We
// avoid nlohmann's NLOHMANN_DEFINE_TYPE_INTRUSIVE so we can map
// snake_case C++ fields to camelCase JSON keys per the LSP wire format.
// ---------------------------------------------------------------------------

Json ToJson(const Position &v);
Json ToJson(const Range &v);
Json ToJson(const Location &v);
Json ToJson(const Diagnostic &v);
Json ToJson(const TextEdit &v);
Json ToJson(const WorkspaceEdit &v);
Json ToJson(const MarkupContent &v);
Json ToJson(const Hover &v);
Json ToJson(const CompletionItem &v);
Json ToJson(const ParameterInformation &v);
Json ToJson(const SignatureInformation &v);
Json ToJson(const SignatureHelp &v);
Json ToJson(const SymbolInformation &v);
Json ToJson(const DocumentSymbol &v);
Json ToJson(const CodeAction &v);
Json ToJson(const TextDocumentIdentifier &v);
Json ToJson(const VersionedTextDocumentIdentifier &v);
Json ToJson(const TextDocumentItem &v);
Json ToJson(const TextDocumentContentChangeEvent &v);
Json ToJson(const DidOpenParams &v);
Json ToJson(const DidChangeParams &v);
Json ToJson(const DidCloseParams &v);
Json ToJson(const DidSaveParams &v);
Json ToJson(const PublishDiagnosticsParams &v);
Json ToJson(const ServerCapabilities &v);
Json ToJson(const InitializeParams &v);
Json ToJson(const InitializeResult &v);

void FromJson(const Json &j, Position &v);
void FromJson(const Json &j, Range &v);
void FromJson(const Json &j, Location &v);
void FromJson(const Json &j, Diagnostic &v);
void FromJson(const Json &j, TextEdit &v);
void FromJson(const Json &j, WorkspaceEdit &v);
void FromJson(const Json &j, MarkupContent &v);
void FromJson(const Json &j, Hover &v);
void FromJson(const Json &j, CompletionItem &v);
void FromJson(const Json &j, ParameterInformation &v);
void FromJson(const Json &j, SignatureInformation &v);
void FromJson(const Json &j, SignatureHelp &v);
void FromJson(const Json &j, SymbolInformation &v);
void FromJson(const Json &j, DocumentSymbol &v);
void FromJson(const Json &j, CodeAction &v);
void FromJson(const Json &j, TextDocumentIdentifier &v);
void FromJson(const Json &j, VersionedTextDocumentIdentifier &v);
void FromJson(const Json &j, TextDocumentItem &v);
void FromJson(const Json &j, TextDocumentContentChangeEvent &v);
void FromJson(const Json &j, DidOpenParams &v);
void FromJson(const Json &j, DidChangeParams &v);
void FromJson(const Json &j, DidCloseParams &v);
void FromJson(const Json &j, DidSaveParams &v);
void FromJson(const Json &j, PublishDiagnosticsParams &v);
void FromJson(const Json &j, ServerCapabilities &v);
void FromJson(const Json &j, InitializeParams &v);

// ---------------------------------------------------------------------------
// JSON-RPC framing helpers
// ---------------------------------------------------------------------------

/// Build a JSON-RPC 2.0 request envelope.
Json MakeRequest(int id, const std::string &method, const Json &params);

/// Build a JSON-RPC 2.0 notification envelope (no id).
Json MakeNotification(const std::string &method, const Json &params);

/// Build a JSON-RPC 2.0 success response.
Json MakeResponse(int id, const Json &result);

/// Build a JSON-RPC 2.0 error response (LSP error code + message).
Json MakeErrorResponse(int id, int code, const std::string &message);

/// Encode a JSON payload with the LSP `Content-Length` header framing.
std::string EncodeFrame(const Json &payload);

/// Try to consume one framed message from the given buffer in place.
/// Returns true and assigns @p out_payload on success; returns false when
/// the buffer does not yet contain a complete frame (the buffer is left
/// untouched in that case).
bool TryDecodeFrame(std::string &buffer, Json &out_payload);

}  // namespace polyglot::tools::ui::lsp
