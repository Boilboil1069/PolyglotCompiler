/**
 * @file     polyls_server.h
 * @brief    polyls — PolyglotCompiler self-hosted Language Server (core)
 *
 * Responsibilities (per demand 2026-04-28-19 §2):
 *   • initialize / initialized / shutdown / exit lifecycle.
 *   • textDocument/didOpen, didChange (full-sync), didSave, didClose.
 *   • textDocument/publishDiagnostics derived from PloyLanguageFrontend::
 *     Analyze(), which performs lex + parse + sema and reports both
 *     syntactic and semantic errors as @ref frontends::Diagnostic
 *     entries.
 *
 * Other capabilities (completion / hover / definition / …) are left
 * advertised-as-false and are routed back to the client as
 * `MethodNotFound` errors; richer features are deferred to demand
 * items 2026-04-28-21..23.
 *
 * The server is transport-agnostic.  Production drivers wire it to
 * stdio (see `polyls.cpp`); unit tests drive it in-process and capture
 * the outbound JSON envelopes through @ref SetSendHandler.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include "tools/ui/common/lsp/lsp_message.h"

namespace polyglot::polyls {

using Json = polyglot::tools::ui::lsp::Json;

/// In-memory document store keyed by URI.
struct OpenDocument {
  std::string uri;
  std::string language_id;
  std::int32_t version{0};
  std::string text;
};

class PolylsServer {
 public:
  using SendHandler = std::function<void(const Json & /*payload*/)>;

  PolylsServer();

  /// Install the outbound transport callback.  The handler must accept
  /// fully formed JSON-RPC envelopes and is responsible for LSP framing.
  void SetSendHandler(SendHandler handler);

  /// Feed one decoded JSON-RPC payload (request or notification) into
  /// the server.  Responses / notifications are emitted via @ref
  /// SetSendHandler.
  void HandleIncoming(const Json &payload);

  /// True once the client has issued `shutdown` (the server then
  /// rejects any subsequent request other than `exit`).
  bool ShutdownRequested() const { return shutdown_requested_; }

  /// True once the client has issued `exit` — the driver loop should
  /// stop reading stdin and return.
  bool ExitRequested() const { return exit_requested_; }

  /// Snapshot of the currently open documents (test introspection).
  std::vector<OpenDocument> SnapshotDocuments() const;

 private:
  // ── Lifecycle ────────────────────────────────────────────────────────
  void HandleInitialize(int id, const Json &params);
  void HandleShutdown(int id);
  void HandleExit();

  // ── Document sync ────────────────────────────────────────────────────
  void HandleDidOpen(const Json &params);
  void HandleDidChange(const Json &params);
  void HandleDidClose(const Json &params);
  void HandleDidSave(const Json &params);

  // ── Diagnostics ──────────────────────────────────────────────────────
  void RunAndPublishDiagnostics(const std::string &uri);
  static std::string UriToFilesystemPath(const std::string &uri);

  // ── Language features (demand 2026-04-28-21) ────────────────────────
  void HandleCompletion(int id, const Json &params);
  void HandleCompletionResolve(int id, const Json &params);
  void HandleHover(int id, const Json &params);
  void HandleSignatureHelp(int id, const Json &params);

  // ── Wire helpers ─────────────────────────────────────────────────────
  void Send(const Json &payload);
  void SendResponse(int id, const Json &result);
  void SendError(int id, int code, const std::string &message);
  void SendNotification(const std::string &method, const Json &params);

  SendHandler send_handler_;
  bool initialized_{false};
  bool shutdown_requested_{false};
  bool exit_requested_{false};

  mutable std::mutex docs_mu_;
  std::unordered_map<std::string, OpenDocument> documents_;
};

}  // namespace polyglot::polyls
