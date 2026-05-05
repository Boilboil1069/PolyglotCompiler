/**
 * @file     lsp_semantic_tokens_e2e_test.cpp
 * @brief    End-to-end LSP semantic-tokens round-trip against a Ploy
 *           module (demand 2026-04-28-24).
 *
 * Spins up a loopback LSP client, opens a Ploy buffer in-process and
 * verifies that `textDocument/semanticTokens/full` returns the
 * delta-encoded payload that the editor consumes.
 *
 * @ingroup  Tests / integration / LSP
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <string>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_client.h"
#include "tools/ui/common/lsp/lsp_message.h"

namespace lsp = polyglot::tools::ui::lsp;

namespace {

class Harness {
 public:
  Harness() {
    auto pair = lsp::LoopbackTransport::CreatePair();
    client_transport_ = pair.first;
    server_transport_ = pair.second;
    client_ = std::make_shared<lsp::LspClient>(client_transport_);
    server_transport_->SetOnReceive([this](const std::string &chunk) {
      server_buffer_.append(chunk);
      lsp::Json payload;
      while (lsp::TryDecodeFrame(server_buffer_, payload)) {
        server_.HandleIncoming(payload);
      }
    });
    server_.SetSendHandler([this](const lsp::Json &payload) {
      server_transport_->Send(lsp::EncodeFrame(payload));
    });
  }
  std::shared_ptr<lsp::LspClient> client() { return client_; }

 private:
  std::shared_ptr<lsp::LoopbackTransport> client_transport_;
  std::shared_ptr<lsp::LoopbackTransport> server_transport_;
  std::shared_ptr<lsp::LspClient> client_;
  polyglot::polyls::PolylsServer server_;
  std::string server_buffer_;
};

}  // namespace

TEST_CASE("LSP semantic tokens e2e: ploy buffer round-trips delta stream",
          "[lsp][integration][semantic]") {
  Harness h;
  std::atomic<bool> init_done{false};
  lsp::ServerCapabilities advertised;
  lsp::InitializeParams ip;
  ip.process_id = 0;
  h.client()->Initialize(
      ip, [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        lsp::FromJson(result["capabilities"], advertised);
        init_done = true;
      });
  REQUIRE(init_done.load());
  h.client()->Initialized();
  REQUIRE(advertised.semantic_tokens_provider);
  REQUIRE(advertised.semantic_token_types.size() >= 7);
  REQUIRE(advertised.semantic_token_types[6] == "keyword");

  lsp::DidOpenParams open;
  open.text_document.uri = "file:///mem.ploy";
  open.text_document.language_id = "ploy";
  open.text_document.version = 1;
  open.text_document.text =
      "FUNC compute() -> INT {\n"
      "    LET answer = 42;\n"
      "    RETURN answer;\n"
      "}\n";
  h.client()->DidOpen(open);

  std::atomic<bool> got_tokens{false};
  lsp::SemanticTokens st;
  h.client()->SendRequest(
      "textDocument/semanticTokens/full",
      lsp::Json{{"textDocument", {{"uri", "file:///mem.ploy"}}}},
      [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        lsp::FromJson(result, st);
        got_tokens = true;
      });
  REQUIRE(got_tokens.load());
  REQUIRE(!st.data.empty());
  REQUIRE(st.data.size() % 5 == 0);
  REQUIRE(st.data[0] == 0);
  REQUIRE(st.data[1] == 0);
  REQUIRE(st.data[2] == 4);
  REQUIRE(st.data[3] == 6);

  std::atomic<bool> got_range{false};
  lsp::SemanticTokens range_st;
  h.client()->SendRequest(
      "textDocument/semanticTokens/range",
      lsp::Json{{"textDocument", {{"uri", "file:///mem.ploy"}}},
                {"range",
                 {{"start", {{"line", 1}, {"character", 0}}},
                  {"end", {{"line", 1}, {"character", 0}}}}}},
      [&](const lsp::Json &result, const lsp::Json &err) {
        REQUIRE(err.is_null());
        lsp::FromJson(result, range_st);
        got_range = true;
      });
  REQUIRE(got_range.load());
  REQUIRE(range_st.data.size() < st.data.size());
}
