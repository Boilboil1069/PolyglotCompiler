/**
 * @file     lsp_diagnostics_e2e_test.cpp
 * @brief    End-to-end LSP integration: client ↔ polyls in-process
 *
 * Validates that a full lifecycle — initialize, didOpen with malformed
 * .ploy source, publishDiagnostics, didClose, shutdown — completes
 * successfully and that diagnostics produced by the real
 * @ref PloyLanguageFrontend reach the LSP client.  The two endpoints
 * communicate through a @ref polyglot::tools::ui::lsp::LoopbackTransport
 * pair, identical in semantics to the QProcess stdio path used by
 * polyui.
 *
 * Demand: 2026-04-28-19 §5 (集成测试).
 *
 * @ingroup  Tests / integration / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_client.h"
#include "tools/ui/common/lsp/lsp_message.h"

namespace lsp = polyglot::tools::ui::lsp;

namespace {

/// Drive a LoopbackTransport pair: every byte the client sends gets
/// dispatched into the server's HandleIncoming(), and every reply the
/// server sends is decoded and pushed back through the client's transport.
class InProcessHarness {
 public:
  InProcessHarness() {
    auto pair = lsp::LoopbackTransport::CreatePair();
    client_transport_ = pair.first;
    server_transport_ = pair.second;

    client_ = std::make_shared<lsp::LspClient>(client_transport_);

    // Server: pump frames out of server_transport_ into PolylsServer.
    server_transport_->SetOnReceive([this](const std::string &chunk) {
      server_buffer_.append(chunk);
      lsp::Json payload;
      while (lsp::TryDecodeFrame(server_buffer_, payload)) {
        server_.HandleIncoming(payload);
      }
    });
    server_.SetSendHandler([this](const lsp::Json &payload) {
      const std::string framed = lsp::EncodeFrame(payload);
      server_transport_->Send(framed);
    });
  }

  std::shared_ptr<lsp::LspClient> client() { return client_; }
  polyglot::polyls::PolylsServer &server() { return server_; }

 private:
  std::shared_ptr<lsp::LoopbackTransport> client_transport_;
  std::shared_ptr<lsp::LoopbackTransport> server_transport_;
  std::shared_ptr<lsp::LspClient> client_;
  polyglot::polyls::PolylsServer server_;
  std::string server_buffer_;
};

}  // namespace

TEST_CASE("LSP e2e: malformed .ploy yields publishDiagnostics", "[lsp][integration]") {
  InProcessHarness h;

  // Capture publishDiagnostics on the client side.
  std::atomic<int> diag_calls{0};
  std::vector<lsp::Diagnostic> seen;
  std::string seen_uri;
  h.client()->OnPublishDiagnostics([&](const lsp::PublishDiagnosticsParams &p) {
    seen_uri = p.uri;
    seen = p.diagnostics;
    ++diag_calls;
  });

  // Lifecycle: initialize → initialized.
  std::atomic<bool> init_done{false};
  lsp::InitializeParams ip;
  ip.process_id = 0;
  ip.root_uri = "file:///tmp/wsp";
  h.client()->Initialize(ip, [&](const lsp::Json &result, const lsp::Json &error) {
    REQUIRE(error.is_null());
    REQUIRE(result.contains("capabilities"));
    REQUIRE(result["capabilities"]["diagnosticProvider"].get<bool>() == true);
    init_done = true;
  });
  REQUIRE(init_done.load());
  h.client()->Initialized();

  // didOpen with deliberately malformed .ploy source — unterminated string
  // and stray semicolon should both raise lex/parse errors that propagate
  // through the frontend → polyls → publishDiagnostics path.
  lsp::DidOpenParams open;
  open.text_document.uri = "file:///tmp/wsp/bad.ploy";
  open.text_document.language_id = "ploy";
  open.text_document.version = 1;
  open.text_document.text = "fn main() { let x = \"unterminated\n";
  h.client()->DidOpen(open);

  // The loopback path is synchronous, so the server's publishDiagnostics
  // notification has already been dispatched by the time DidOpen returns.
  REQUIRE(diag_calls.load() >= 1);
  REQUIRE(seen_uri == "file:///tmp/wsp/bad.ploy");
  REQUIRE(!seen.empty());
  bool any_error = false;
  for (const auto &d : seen) {
    if (d.severity == lsp::DiagnosticSeverity::kError) {
      any_error = true;
    }
    REQUIRE(d.source == "polyls");
  }
  REQUIRE(any_error);

  // didClose should clear the document's diagnostics (empty publishDiagnostics).
  diag_calls = 0;
  seen.clear();
  lsp::DidCloseParams cl;
  cl.text_document.uri = "file:///tmp/wsp/bad.ploy";
  h.client()->DidClose(cl);
  REQUIRE(diag_calls.load() >= 1);
  REQUIRE(seen.empty());

  // Shutdown then exit — exit_requested becomes true, server returns 0
  // from its driver in production.
  std::atomic<bool> shutdown_done{false};
  h.client()->Shutdown([&](const lsp::Json &result, const lsp::Json &error) {
    REQUIRE(error.is_null());
    (void)result;
    shutdown_done = true;
  });
  REQUIRE(shutdown_done.load());
  h.client()->Exit();
  REQUIRE(h.server().ShutdownRequested());
  REQUIRE(h.server().ExitRequested());
}
