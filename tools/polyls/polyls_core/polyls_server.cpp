/**
 * @file     polyls_server.cpp
 * @brief    Implementation of the polyls language server core
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/polyls/grammar/grammar_descriptor.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <utility>

#include "common/include/version.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_frontend.h"
#include "frontends/ploy/include/ploy_frontend.h"
#include "tools/polyls/polyls_core/symbol_index.h"

namespace polyglot::polyls {

namespace lsp = polyglot::tools::ui::lsp;

namespace {

// JSON-RPC error codes (LSP-aligned subset).
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kServerNotInitialized = -32002;
constexpr int kInvalidShutdownState = -32600;

int FromHexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

}  // namespace

PolylsServer::PolylsServer() = default;

void PolylsServer::SetSendHandler(SendHandler handler) {
  send_handler_ = std::move(handler);
}

void PolylsServer::HandleIncoming(const Json &payload) {
  if (!payload.is_object()) {
    SendError(0, kInvalidRequest, "expected JSON object envelope");
    return;
  }
  const std::string method = payload.value("method", std::string{});
  const bool has_id =
      payload.contains("id") && payload["id"].is_number_integer();
  const int id = has_id ? payload["id"].get<int>() : 0;
  const Json params = payload.value("params", Json::object());

  // Handle exit unconditionally — it must work even before initialize.
  if (method == "exit") {
    HandleExit();
    return;
  }

  if (shutdown_requested_) {
    if (has_id) {
      SendError(id, kInvalidShutdownState,
                "server is shutting down; only `exit` is accepted");
    }
    return;
  }

  if (!initialized_ && method != "initialize") {
    if (has_id) {
      SendError(id, kServerNotInitialized,
                "server has not been initialized");
    }
    return;
  }

  if (method == "initialize") {
    if (!has_id) {
      // initialize must be a request.
      return;
    }
    HandleInitialize(id, params);
    return;
  }
  if (method == "initialized") {
    // Notification; nothing to ack.
    return;
  }
  if (method == "shutdown") {
    HandleShutdown(has_id ? id : 0);
    return;
  }
  if (method == "textDocument/didOpen") {
    HandleDidOpen(params);
    return;
  }
  if (method == "textDocument/didChange") {
    HandleDidChange(params);
    return;
  }
  if (method == "textDocument/didClose") {
    HandleDidClose(params);
    return;
  }
  if (method == "textDocument/didSave") {
    HandleDidSave(params);
    return;
  }

  // ── Language features (demand 2026-04-28-21) ──────────────────────
  if (method == "textDocument/completion") {
    if (has_id) HandleCompletion(id, params);
    return;
  }
  if (method == "completionItem/resolve") {
    if (has_id) HandleCompletionResolve(id, params);
    return;
  }
  if (method == "textDocument/hover") {
    if (has_id) HandleHover(id, params);
    return;
  }
  if (method == "textDocument/signatureHelp") {
    if (has_id) HandleSignatureHelp(id, params);
    return;
  }
  // ── Navigation features (demand 2026-04-28-22) ─────────────────
  if (method == "textDocument/definition") {
    if (has_id) HandleDefinition(id, params);
    return;
  }
  if (method == "textDocument/declaration") {
    if (has_id) HandleDeclaration(id, params);
    return;
  }
  if (method == "textDocument/implementation") {
    if (has_id) HandleImplementation(id, params);
    return;
  }
  if (method == "textDocument/typeDefinition") {
    if (has_id) HandleTypeDefinition(id, params);
    return;
  }
  if (method == "textDocument/references") {
    if (has_id) HandleReferences(id, params);
    return;
  }
  // ── Refactoring features (demand 2026-04-28-23) ────────────
  if (method == "textDocument/prepareRename") {
    if (has_id) HandlePrepareRename(id, params);
    return;
  }
  if (method == "textDocument/rename") {
    if (has_id) HandleRename(id, params);
    return;
  }
  if (method == "textDocument/codeAction") {
    if (has_id) HandleCodeAction(id, params);
    return;
  }
  // ── Semantic tokens (demand 2026-04-28-24) ─────────────────
  if (method == "textDocument/semanticTokens/full") {
    if (has_id) HandleSemanticTokensFull(id, params);
    return;
  }
  if (method == "textDocument/semanticTokens/range") {
    if (has_id) HandleSemanticTokensRange(id, params);
    return;
  }
  // ── Symbols & navigation panels (demand 2026-04-28-25) ────
  if (method == "textDocument/documentSymbol") {
    if (has_id) HandleDocumentSymbol(id, params);
    return;
  }
  if (method == "workspace/symbol") {
    if (has_id) HandleWorkspaceSymbol(id, params);
    return;
  }
  // ── Formatting (demand 2026-04-28-26) ─────────────────────
  if (method == "textDocument/formatting") {
    if (has_id) HandleFormatting(id, params);
    return;
  }
  if (method == "textDocument/rangeFormatting") {
    if (has_id) HandleRangeFormatting(id, params);
    return;
  }
  if (method == "textDocument/onTypeFormatting") {
    if (has_id) HandleOnTypeFormatting(id, params);
    return;
  }
  if (has_id) {
    SendError(id, kMethodNotFound, "method not implemented: " + method);
  }
  // Unknown notifications are dropped silently per JSON-RPC.
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void PolylsServer::HandleInitialize(int id, const Json &params) {
  initialized_ = true;

  // Establish workspace cache directory from the client's rootUri.
  cache_dir_.clear();
  if (params.is_object() && params.contains("rootUri") &&
      params["rootUri"].is_string()) {
    const std::string root_uri = params["rootUri"].get<std::string>();
    const std::string root_path = UriToPath(root_uri);
    if (!root_path.empty()) {
      const std::filesystem::path cache =
          std::filesystem::path(root_path) / ".polyc-cache";
      cache_dir_ = cache.string();
      std::error_code ec;
      std::filesystem::create_directories(cache, ec);
      // Best-effort: warm-load any prior snapshot.  Failure is silent
      // because a fresh workspace simply has no cache file yet.
      (void)index_->LoadFromCache(cache_dir_);
    }
  }

  lsp::InitializeResult result;
  result.server_name = "polyls";
  result.server_version = POLYGLOT_VERSION_STRING;
  // Sync + diagnostics (demand 2026-04-28-19), language features
  // (demand 2026-04-28-21), navigation (demand 2026-04-28-22),
  // refactoring (demand 2026-04-28-23), semantic tokens
  // (demand 2026-04-28-24).
  result.capabilities.text_document_sync = 1;  // full
  result.capabilities.diagnostic_provider = true;
  result.capabilities.completion_provider = true;
  result.capabilities.hover_provider = true;
  result.capabilities.signature_help_provider = true;
  result.capabilities.definition_provider = true;
  result.capabilities.declaration_provider = true;
  result.capabilities.implementation_provider = true;
  result.capabilities.type_definition_provider = true;
  result.capabilities.references_provider = true;
  result.capabilities.rename_provider = true;
  result.capabilities.code_action_provider = true;
  result.capabilities.document_symbol_provider = true;
  result.capabilities.workspace_symbol_provider = true;
  result.capabilities.document_formatting_provider = true;
  result.capabilities.document_range_formatting_provider = true;
  result.capabilities.document_on_type_formatting_provider = true;
  result.capabilities.on_type_formatting_first_trigger = "\n";
  result.capabilities.semantic_tokens_provider = true;
  result.capabilities.semantic_token_types.assign(
      polyglot::polyls::grammar::kTokenTypes.begin(),
      polyglot::polyls::grammar::kTokenTypes.end());
  result.capabilities.semantic_token_modifiers.assign(
      polyglot::polyls::grammar::kTokenModifiers.begin(),
      polyglot::polyls::grammar::kTokenModifiers.end());
  SendResponse(id, lsp::ToJson(result));
}

void PolylsServer::HandleShutdown(int id) {
  shutdown_requested_ = true;
  // Persist the latest workspace snapshot on the way out so the next
  // editor session can answer navigation queries instantly.
  if (!cache_dir_.empty()) {
    (void)index_->SaveToCache(cache_dir_);
  }
  if (id > 0) SendResponse(id, Json());
}

void PolylsServer::HandleExit() { exit_requested_ = true; }

// ---------------------------------------------------------------------------
// Document sync
// ---------------------------------------------------------------------------

void PolylsServer::HandleDidOpen(const Json &params) {
  lsp::DidOpenParams p;
  lsp::FromJson(params, p);
  if (p.text_document.uri.empty()) {
    SendError(0, kInvalidParams, "didOpen: missing textDocument.uri");
    return;
  }
  OpenDocument doc;
  doc.uri = p.text_document.uri;
  doc.language_id = p.text_document.language_id;
  doc.version = p.text_document.version;
  doc.text = p.text_document.text;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    documents_[doc.uri] = std::move(doc);
  }
  RefreshIndexFor(p.text_document.uri);
  RunAndPublishDiagnostics(p.text_document.uri);
}

void PolylsServer::HandleDidChange(const Json &params) {
  lsp::DidChangeParams p;
  lsp::FromJson(params, p);
  if (p.text_document.uri.empty()) {
    SendError(0, kInvalidParams, "didChange: missing textDocument.uri");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(p.text_document.uri);
    if (it == documents_.end()) return;  // didChange before didOpen — ignore.
    it->second.version = p.text_document.version;
    // Full-sync only (capabilities advertise textDocumentSync = 1).  When
    // multiple change events arrive in one envelope, the last one wins.
    for (const auto &change : p.content_changes) {
      if (!change.range) {
        it->second.text = change.text;
      }
    }
  }
  RefreshIndexFor(p.text_document.uri);
  RunAndPublishDiagnostics(p.text_document.uri);
}

void PolylsServer::HandleDidClose(const Json &params) {
  lsp::DidCloseParams p;
  lsp::FromJson(params, p);
  if (p.text_document.uri.empty()) return;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    documents_.erase(p.text_document.uri);
  }
  // Drop the document from the symbol index too, but keep the cache
  // file intact so we can answer historical references on next start.
  index_->RemoveDocument(p.text_document.uri);
  // Publish an empty diagnostics list so the editor clears its overlay.
  lsp::PublishDiagnosticsParams empty;
  empty.uri = p.text_document.uri;
  SendNotification("textDocument/publishDiagnostics", lsp::ToJson(empty));
}

void PolylsServer::HandleDidSave(const Json &params) {
  lsp::DidSaveParams p;
  lsp::FromJson(params, p);
  if (p.text_document.uri.empty()) return;
  if (p.text) {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(p.text_document.uri);
    if (it != documents_.end()) it->second.text = *p.text;
  }
  RefreshIndexFor(p.text_document.uri);
  RunAndPublishDiagnostics(p.text_document.uri);
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void PolylsServer::RunAndPublishDiagnostics(const std::string &uri) {
  std::string source;
  std::string language_id;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) return;
    source = it->second.text;
    language_id = it->second.language_id;
  }

  lsp::PublishDiagnosticsParams out;
  out.uri = uri;

  // polyls only owns `.ploy` analysis in this revision.  For other
  // language ids we publish an empty list so the editor clears any
  // previous overlay.
  if (language_id == "ploy" || language_id == "poly") {
    polyglot::frontends::Diagnostics diags;
    polyglot::frontends::FrontendOptions opts;
    opts.strict = false;
    polyglot::ploy::PloyLanguageFrontend frontend;
    const std::string filename = UriToFilesystemPath(uri);
    (void)frontend.Analyze(source, filename, diags, opts);

    for (const auto &d : diags.All()) {
      lsp::Diagnostic out_diag;
      // Compiler is 1-based; LSP is 0-based.
      const std::uint32_t line =
          d.loc.line > 0 ? static_cast<std::uint32_t>(d.loc.line - 1) : 0;
      const std::uint32_t col =
          d.loc.column > 0 ? static_cast<std::uint32_t>(d.loc.column - 1) : 0;
      out_diag.range.start = lsp::Position{line, col};
      // Single-token range; clients widen it as needed for squigglies.
      out_diag.range.end = lsp::Position{line, col + 1};
      switch (d.severity) {
        case polyglot::frontends::DiagnosticSeverity::kError:
          out_diag.severity = lsp::DiagnosticSeverity::kError;
          break;
        case polyglot::frontends::DiagnosticSeverity::kWarning:
          out_diag.severity = lsp::DiagnosticSeverity::kWarning;
          break;
        case polyglot::frontends::DiagnosticSeverity::kNote:
          out_diag.severity = lsp::DiagnosticSeverity::kInformation;
          break;
        default:
          out_diag.severity = lsp::DiagnosticSeverity::kInformation;
          break;
      }
      if (d.code != polyglot::frontends::ErrorCode::kUnknown) {
        out_diag.code = "E" + std::to_string(static_cast<int>(d.code));
      }
      out_diag.source = "polyls";
      out_diag.message = d.message;
      out.diagnostics.push_back(std::move(out_diag));
    }
  }

  SendNotification("textDocument/publishDiagnostics", lsp::ToJson(out));
}

std::string PolylsServer::UriToFilesystemPath(const std::string &uri) {
  // Best-effort `file://` decoder — sufficient for the diagnostics
  // pipeline (the filename is used purely for display in error messages).
  constexpr const char *kPrefix = "file://";
  std::string body = uri;
  if (body.rfind(kPrefix, 0) == 0) {
    body.erase(0, std::strlen(kPrefix));
    // Windows: "file:///C:/foo" → "/C:/foo"; strip the leading slash.
    if (body.size() >= 3 && body[0] == '/' && std::isalpha(static_cast<unsigned char>(body[1])) &&
        body[2] == ':') {
      body.erase(0, 1);
    }
  }

  // Percent-decode.
  std::string out;
  out.reserve(body.size());
  for (std::size_t i = 0; i < body.size(); ++i) {
    if (body[i] == '%' && i + 2 < body.size()) {
      const int hi = FromHexDigit(body[i + 1]);
      const int lo = FromHexDigit(body[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(body[i]);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Wire helpers
// ---------------------------------------------------------------------------

void PolylsServer::Send(const Json &payload) {
  if (send_handler_) send_handler_(payload);
}

void PolylsServer::SendResponse(int id, const Json &result) {
  Send(lsp::MakeResponse(id, result));
}

void PolylsServer::SendError(int id, int code, const std::string &message) {
  Send(lsp::MakeErrorResponse(id, code, message));
}

void PolylsServer::SendNotification(const std::string &method, const Json &params) {
  Send(lsp::MakeNotification(method, params));
}

std::vector<OpenDocument> PolylsServer::SnapshotDocuments() const {
  std::lock_guard<std::mutex> lock(docs_mu_);
  std::vector<OpenDocument> out;
  out.reserve(documents_.size());
  for (const auto &kv : documents_) out.push_back(kv.second);
  return out;
}

// ---------------------------------------------------------------------------
// SymbolIndex bridging
// ---------------------------------------------------------------------------

std::string PolylsServer::IndexLanguageFor(const std::string &uri) const {
  std::lock_guard<std::mutex> lock(docs_mu_);
  auto it = documents_.find(uri);
  if (it == documents_.end()) return {};
  return it->second.language_id;
}

void PolylsServer::RefreshIndexFor(const std::string &uri) {
  std::string text;
  std::string language_id;
  {
    std::lock_guard<std::mutex> lock(docs_mu_);
    auto it = documents_.find(uri);
    if (it == documents_.end()) return;
    text = it->second.text;
    language_id = it->second.language_id;
  }
  index_->IndexDocument(uri, language_id, text);
  // Best-effort cache write — swallow filesystem failures because the
  // editor is the source of truth and the cache is purely a warm path.
  if (!cache_dir_.empty()) (void)index_->SaveToCache(cache_dir_);
}

}  // namespace polyglot::polyls
