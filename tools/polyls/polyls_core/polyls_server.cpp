/**
 * @file     polyls_server.cpp
 * @brief    Implementation of the polyls language server core
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/polyls/polyls_core/polyls_server.h"

#include <cctype>
#include <cstdio>
#include <utility>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_frontend.h"
#include "frontends/ploy/include/ploy_frontend.h"

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

  if (has_id) {
    SendError(id, kMethodNotFound, "method not implemented: " + method);
  }
  // Unknown notifications are dropped silently per JSON-RPC.
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void PolylsServer::HandleInitialize(int id, const Json & /*params*/) {
  initialized_ = true;
  lsp::InitializeResult result;
  result.server_name = "polyls";
  result.server_version = "1.22.0";
  // Sync + diagnostics (demand 2026-04-28-19) and language features
  // (demand 2026-04-28-21): completion, hover, signatureHelp.
  result.capabilities.text_document_sync = 1;  // full
  result.capabilities.diagnostic_provider = true;
  result.capabilities.completion_provider = true;
  result.capabilities.hover_provider = true;
  result.capabilities.signature_help_provider = true;
  SendResponse(id, lsp::ToJson(result));
}

void PolylsServer::HandleShutdown(int id) {
  shutdown_requested_ = true;
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

}  // namespace polyglot::polyls
