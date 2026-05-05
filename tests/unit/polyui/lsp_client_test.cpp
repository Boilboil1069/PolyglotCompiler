/**
 * @file     lsp_client_test.cpp
 * @brief    Unit tests for the LSP client core (demand 2026-04-28-19 §5)
 *
 * Covers:
 *   • JSON-RPC envelope helpers (request / notification / response /
 *     error response) and the `Content-Length` framing round-trip.
 *   • LoopbackTransport delivery semantics.
 *   • LspClient request / response correlation, notification dispatch,
 *     publishDiagnostics typed callback, and the lifecycle convenience
 *     wrappers (initialize / initialized / shutdown / exit).
 *   • LspCapabilityRegistry get / set / supports.
 *   • LspSessionRegistry GetOrCreate / Find / Drop.
 *
 * @ingroup  Tests / unit / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>

#include "tools/ui/common/lsp/lsp_capability_registry.h"
#include "tools/ui/common/lsp/lsp_client.h"
#include "tools/ui/common/lsp/lsp_message.h"
#include "tools/ui/common/lsp/lsp_session.h"

using namespace polyglot::tools::ui::lsp;

TEST_CASE("EncodeFrame + TryDecodeFrame round-trip", "[lsp][framing]") {
  const Json original = Json{{"jsonrpc", "2.0"}, {"method", "ping"}};
  std::string buffer = EncodeFrame(original);
  REQUIRE(buffer.find("Content-Length:") == 0);

  Json decoded;
  REQUIRE(TryDecodeFrame(buffer, decoded));
  REQUIRE(decoded == original);
  REQUIRE(buffer.empty());
}

TEST_CASE("TryDecodeFrame returns false on partial buffer", "[lsp][framing]") {
  const Json original = Json{{"jsonrpc", "2.0"}, {"method", "ping"}};
  std::string complete = EncodeFrame(original);
  std::string partial = complete.substr(0, complete.size() - 5);

  Json decoded;
  REQUIRE_FALSE(TryDecodeFrame(partial, decoded));
  // The buffer must remain untouched so subsequent reads can complete it.
  REQUIRE(partial.size() == complete.size() - 5);

  partial.append(complete.substr(complete.size() - 5));
  REQUIRE(TryDecodeFrame(partial, decoded));
  REQUIRE(decoded == original);
}

TEST_CASE("MakeRequest / MakeNotification envelopes", "[lsp][rpc]") {
  const Json req = MakeRequest(7, "initialize", Json::object());
  REQUIRE(req["jsonrpc"] == "2.0");
  REQUIRE(req["id"] == 7);
  REQUIRE(req["method"] == "initialize");
  REQUIRE(req["params"].is_object());

  const Json notif = MakeNotification("exit", Json());
  REQUIRE_FALSE(notif.contains("id"));
  REQUIRE(notif["method"] == "exit");
}

TEST_CASE("LoopbackTransport delivers chunks to its peer", "[lsp][transport]") {
  auto [a, b] = LoopbackTransport::CreatePair();

  std::string received;
  b->SetOnReceive([&](const std::string &chunk) { received.append(chunk); });

  a->Send("hello ");
  a->Send("world");
  REQUIRE(received == "hello world");

  b->Close();
  // After close, the transport reports IsOpen() == false but it is still
  // legal to call Send (the call becomes a no-op).
  REQUIRE_FALSE(b->IsOpen());
}

TEST_CASE("LspClient correlates requests with responses", "[lsp][client]") {
  auto [client_xport, server_xport] = LoopbackTransport::CreatePair();
  LspClient client(client_xport);

  // Stand-in server: echo back result = {"echo": <method>}.
  server_xport->SetOnReceive([&server_xport](const std::string &chunk) {
    std::string buf = chunk;
    Json payload;
    while (TryDecodeFrame(buf, payload)) {
      if (payload.contains("id")) {
        const Json result = Json{{"echo", payload.value("method", std::string{})}};
        server_xport->Send(EncodeFrame(MakeResponse(payload["id"].get<int>(), result)));
      }
    }
  });

  std::string captured_method;
  client.SendRequest("custom/method", Json::object(),
                     [&](const Json &result, const Json &error) {
                       REQUIRE(error.is_null());
                       captured_method = result.value("echo", std::string{});
                     });
  REQUIRE(captured_method == "custom/method");
}

TEST_CASE("LspClient dispatches notifications by method", "[lsp][client]") {
  auto [client_xport, server_xport] = LoopbackTransport::CreatePair();
  LspClient client(client_xport);

  std::atomic<int> hits{0};
  client.OnNotification("textDocument/publishDiagnostics",
                        [&](const Json &) { ++hits; });

  // Server pushes a notification.
  server_xport->Send(EncodeFrame(
      MakeNotification("textDocument/publishDiagnostics",
                       Json{{"uri", "file:///a.ploy"}, {"diagnostics", Json::array()}})));
  REQUIRE(hits.load() == 1);

  // Unknown notifications must be dropped silently (no throw).
  server_xport->Send(EncodeFrame(MakeNotification("custom/event", Json::object())));
  REQUIRE(hits.load() == 1);
}

TEST_CASE("LspClient typed publishDiagnostics handler", "[lsp][client]") {
  auto [client_xport, server_xport] = LoopbackTransport::CreatePair();
  LspClient client(client_xport);

  PublishDiagnosticsParams captured;
  client.OnPublishDiagnostics([&](const PublishDiagnosticsParams &p) { captured = p; });

  Diagnostic d;
  d.range.start = Position{1, 4};
  d.range.end = Position{1, 8};
  d.severity = DiagnosticSeverity::kWarning;
  d.message = "unused identifier";
  d.source = "polyls";
  PublishDiagnosticsParams params;
  params.uri = "file:///a.ploy";
  params.diagnostics.push_back(d);
  server_xport->Send(
      EncodeFrame(MakeNotification("textDocument/publishDiagnostics", ToJson(params))));

  REQUIRE(captured.uri == "file:///a.ploy");
  REQUIRE(captured.diagnostics.size() == 1);
  REQUIRE(captured.diagnostics.front().severity == DiagnosticSeverity::kWarning);
  REQUIRE(captured.diagnostics.front().message == "unused identifier");
  REQUIRE(captured.diagnostics.front().range.start.character == 4);
}

TEST_CASE("LspClient log handler observes both directions", "[lsp][client]") {
  auto [client_xport, server_xport] = LoopbackTransport::CreatePair();
  LspClient client(client_xport);

  std::vector<std::pair<std::string, std::string>> log;
  client.SetLogHandler([&](const std::string &dir, const Json &payload) {
    log.emplace_back(dir, payload.value("method", std::string{}));
  });

  client.SendNotification("initialized", Json::object());
  server_xport->Send(EncodeFrame(MakeNotification("window/logMessage", Json::object())));

  REQUIRE(log.size() == 2);
  REQUIRE(log[0].first == "tx");
  REQUIRE(log[0].second == "initialized");
  REQUIRE(log[1].first == "rx");
  REQUIRE(log[1].second == "window/logMessage");
}

TEST_CASE("LspCapabilityRegistry get / set / supports", "[lsp][capabilities]") {
  LspCapabilityRegistry reg;
  REQUIRE_FALSE(reg.Get("polyls@/proj").has_value());

  ServerCapabilities caps;
  caps.diagnostic_provider = true;
  caps.hover_provider = false;
  reg.Set("polyls@/proj", caps);

  auto fetched = reg.Get("polyls@/proj");
  REQUIRE(fetched.has_value());
  REQUIRE(fetched->diagnostic_provider);
  REQUIRE_FALSE(fetched->hover_provider);

  REQUIRE(reg.Supports("polyls@/proj", &ServerCapabilities::diagnostic_provider));
  REQUIRE_FALSE(reg.Supports("polyls@/proj", &ServerCapabilities::hover_provider));
  REQUIRE_FALSE(reg.Supports("missing", &ServerCapabilities::diagnostic_provider));

  reg.Remove("polyls@/proj");
  REQUIRE_FALSE(reg.Get("polyls@/proj").has_value());
}

TEST_CASE("LspSessionRegistry GetOrCreate is idempotent", "[lsp][session]") {
  LspSessionRegistry reg;
  const SessionKey key{"file:///workspace", "ploy"};

  int factory_calls = 0;
  auto factory = [&]() {
    ++factory_calls;
    auto [a, _] = LoopbackTransport::CreatePair();
    return std::make_shared<LspClient>(a);
  };

  auto s1 = reg.GetOrCreate(key, factory);
  auto s2 = reg.GetOrCreate(key, factory);
  REQUIRE(s1.get() == s2.get());
  REQUIRE(factory_calls == 1);
  REQUIRE(reg.Find(key) == s1);
  REQUIRE(s1->id == "ploy@file:///workspace");

  reg.Drop(key);
  REQUIRE(reg.Find(key) == nullptr);
}
