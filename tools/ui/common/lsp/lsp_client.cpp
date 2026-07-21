/**
 * @file     lsp_client.cpp
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/lsp/lsp_client.h"

#include <utility>

namespace polyglot::tools::ui::lsp {

// ---------------------------------------------------------------------------
// LoopbackTransport
// ---------------------------------------------------------------------------

std::pair<std::shared_ptr<LoopbackTransport>,
          std::shared_ptr<LoopbackTransport>>
LoopbackTransport::CreatePair() {
  auto a = std::shared_ptr<LoopbackTransport>(new LoopbackTransport());
  auto b = std::shared_ptr<LoopbackTransport>(new LoopbackTransport());
  a->peer_ = b;
  b->peer_ = a;
  return {a, b};
}

void LoopbackTransport::Send(const std::string &bytes) {
  if (!open_) return;
  auto peer = peer_.lock();
  if (!peer) return;

  ReceiveCallback cb;
  {
    std::lock_guard<std::mutex> lock(peer->mu_);
    cb = peer->on_receive_;
  }
  if (cb) cb(bytes);
}

void LoopbackTransport::SetOnReceive(ReceiveCallback cb) {
  std::lock_guard<std::mutex> lock(mu_);
  on_receive_ = std::move(cb);
}

void LoopbackTransport::Close() { open_ = false; }

// ---------------------------------------------------------------------------
// LspClient
// ---------------------------------------------------------------------------

LspClient::LspClient(std::shared_ptr<ILspTransport> transport)
    : transport_(std::move(transport)) {
  if (transport_) {
    transport_->SetOnReceive(
        [this](const std::string &chunk) { HandleChunk(chunk); });
  }
}

LspClient::~LspClient() {
  if (transport_) transport_->SetOnReceive({});
}

int LspClient::SendRequest(const std::string &method, const Json &params,
                           ResponseHandler on_response) {
  int id = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    id = next_id_++;
    if (on_response) pending_[id] = std::move(on_response);
  }
  const Json envelope = MakeRequest(id, method, params);
  if (log_handler_) log_handler_("tx", envelope);
  if (transport_) transport_->Send(EncodeFrame(envelope));
  return id;
}

void LspClient::SendNotification(const std::string &method, const Json &params) {
  const Json envelope = MakeNotification(method, params);
  if (log_handler_) log_handler_("tx", envelope);
  if (transport_) transport_->Send(EncodeFrame(envelope));
}

void LspClient::OnNotification(const std::string &method,
                               NotificationHandler handler) {
  std::lock_guard<std::mutex> lock(mu_);
  notif_handlers_[method] = std::move(handler);
}

void LspClient::SetLogHandler(LogHandler handler) {
  std::lock_guard<std::mutex> lock(mu_);
  log_handler_ = std::move(handler);
}

// -- Lifecycle / sync convenience wrappers ------------------------------------

void LspClient::Initialize(const InitializeParams &params,
                           ResponseHandler on_response) {
  SendRequest("initialize", ToJson(params), std::move(on_response));
}

void LspClient::Initialized() {
  SendNotification("initialized", Json::object());
}

void LspClient::Shutdown(ResponseHandler on_response) {
  SendRequest("shutdown", Json(), std::move(on_response));
}

void LspClient::Exit() { SendNotification("exit", Json()); }

void LspClient::DidOpen(const DidOpenParams &params) {
  SendNotification("textDocument/didOpen", ToJson(params));
}

void LspClient::DidChange(const DidChangeParams &params) {
  SendNotification("textDocument/didChange", ToJson(params));
}

void LspClient::DidClose(const DidCloseParams &params) {
  SendNotification("textDocument/didClose", ToJson(params));
}

void LspClient::DidSave(const DidSaveParams &params) {
  SendNotification("textDocument/didSave", ToJson(params));
}

void LspClient::OnPublishDiagnostics(
    std::function<void(const PublishDiagnosticsParams &)> cb) {
  OnNotification("textDocument/publishDiagnostics",
                 [cb = std::move(cb)](const Json &params) {
                   PublishDiagnosticsParams p;
                   FromJson(params, p);
                   cb(p);
                 });
}

// -- Receive / dispatch -------------------------------------------------------

void LspClient::HandleChunk(const std::string &chunk) {
  rx_buffer_.append(chunk);
  Json payload;
  while (TryDecodeFrame(rx_buffer_, payload)) {
    DispatchPayload(payload);
  }
}

void LspClient::DispatchPayload(const Json &payload) {
  if (log_handler_) log_handler_("rx", payload);

  // Response (has "id" and either "result" or "error", no "method").
  if (payload.contains("id") && !payload.contains("method")) {
    int id = 0;
    if (payload["id"].is_number_integer()) id = payload["id"].get<int>();
    ResponseHandler handler;
    {
      std::lock_guard<std::mutex> lock(mu_);
      auto it = pending_.find(id);
      if (it != pending_.end()) {
        handler = std::move(it->second);
        pending_.erase(it);
      }
    }
    if (handler) {
      const Json result = payload.value("result", Json());
      const Json error = payload.value("error", Json());
      handler(result, error);
    }
    return;
  }

  // Notification (has "method", no "id") — or server-initiated request,
  // which polyls (and the demand) does not currently emit; we treat any
  // method-bearing message as a notification dispatch.
  if (payload.contains("method")) {
    const std::string method = payload["method"].get<std::string>();
    NotificationHandler handler;
    {
      std::lock_guard<std::mutex> lock(mu_);
      auto it = notif_handlers_.find(method);
      if (it != notif_handlers_.end()) handler = it->second;
    }
    if (handler) handler(payload.value("params", Json::object()));
  }
}

}  // namespace polyglot::tools::ui::lsp
