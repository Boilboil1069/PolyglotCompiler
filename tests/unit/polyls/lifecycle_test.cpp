/**
 * @file     lifecycle_test.cpp
 * @brief    Unit tests for polyls lifecycle + sync + diagnostics
 *           (demand 2026-04-28-19 §5)
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_message.h"

using polyglot::polyls::PolylsServer;
using polyglot::tools::ui::lsp::Json;
using polyglot::tools::ui::lsp::MakeNotification;
using polyglot::tools::ui::lsp::MakeRequest;

namespace {

struct Captured {
  std::vector<Json> outbound;
};

void MakeServer(PolylsServer &s, Captured &capture) {
  s.SetSendHandler([&](const Json &payload) { capture.outbound.push_back(payload); });
}

const Json *FindResponse(const Captured &cap, int id) {
  for (const auto &p : cap.outbound) {
    if (p.value("id", -1) == id) return &p;
  }
  return nullptr;
}

const Json *FindNotification(const Captured &cap, const std::string &method) {
  for (const auto &p : cap.outbound) {
    if (p.value("method", std::string{}) == method) return &p;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("polyls rejects non-initialize requests before initialization",
          "[polyls][lifecycle]") {
  Captured cap;
  PolylsServer s; MakeServer(s, cap);

  s.HandleIncoming(MakeRequest(1, "shutdown", Json()));
  const Json *resp = FindResponse(cap, 1);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("error"));
  REQUIRE((*resp)["error"]["code"] == -32002);  // ServerNotInitialized
}

TEST_CASE("polyls initialize advertises only sync + diagnostics",
          "[polyls][lifecycle]") {
  Captured cap;
  PolylsServer s; MakeServer(s, cap);

  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  const Json *resp = FindResponse(cap, 1);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("result"));
  const Json &caps = (*resp)["result"]["capabilities"];
  REQUIRE(caps["textDocumentSync"] == 1);
  REQUIRE(caps["diagnosticProvider"] == true);
  // Language features added by demand 2026-04-28-21.
  REQUIRE(caps["hoverProvider"] == true);
  REQUIRE(caps["completionProvider"] == true);
  REQUIRE(caps["signatureHelpProvider"] == true);
  // Navigation features added by demand 2026-04-28-22.
  REQUIRE(caps["definitionProvider"] == true);
  REQUIRE(caps["declarationProvider"] == true);
  REQUIRE(caps["implementationProvider"] == true);
  REQUIRE(caps["typeDefinitionProvider"] == true);
  REQUIRE(caps["referencesProvider"] == true);
  REQUIRE(caps["renameProvider"] == true);
  REQUIRE(caps["codeActionProvider"] == true);
  // Semantic tokens added by demand 2026-04-28-24.
  REQUIRE(caps["semanticTokensProvider"].is_object());
  REQUIRE(caps["semanticTokensProvider"]["full"] == true);
  REQUIRE(caps["semanticTokensProvider"]["range"] == true);
  REQUIRE(caps["semanticTokensProvider"]["legend"]["tokenTypes"]
              .is_array());
}

TEST_CASE("polyls didOpen + didChange + didClose document store",
          "[polyls][sync]") {
  Captured cap;
  PolylsServer s; MakeServer(s, cap);
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  s.HandleIncoming(MakeNotification("initialized", Json::object()));

  // Trivially valid module.
  const Json open_params = Json{
      {"textDocument",
       {{"uri", "file:///a.ploy"},
        {"languageId", "ploy"},
        {"version", 1},
        {"text", "FUNC f() { }\n"}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open_params));

  auto docs = s.SnapshotDocuments();
  REQUIRE(docs.size() == 1);
  REQUIRE(docs.front().uri == "file:///a.ploy");
  REQUIRE(docs.front().version == 1);
  REQUIRE(docs.front().text == "FUNC f() { }\n");

  // didChange (full sync — no range).
  const Json change_params = Json{
      {"textDocument", {{"uri", "file:///a.ploy"}, {"version", 2}}},
      {"contentChanges", Json::array({Json{{"text", "FUNC g() { }\n"}}})}};
  s.HandleIncoming(MakeNotification("textDocument/didChange", change_params));
  docs = s.SnapshotDocuments();
  REQUIRE(docs.front().version == 2);
  REQUIRE(docs.front().text == "FUNC g() { }\n");

  // didClose removes the entry and emits an empty publishDiagnostics.
  cap.outbound.clear();
  const Json close_params =
      Json{{"textDocument", {{"uri", "file:///a.ploy"}}}};
  s.HandleIncoming(MakeNotification("textDocument/didClose", close_params));
  REQUIRE(s.SnapshotDocuments().empty());

  const Json *empty = FindNotification(cap, "textDocument/publishDiagnostics");
  REQUIRE(empty != nullptr);
  REQUIRE((*empty)["params"]["uri"] == "file:///a.ploy");
  REQUIRE((*empty)["params"]["diagnostics"].is_array());
  REQUIRE((*empty)["params"]["diagnostics"].empty());
}

TEST_CASE("polyls publishDiagnostics carries syntax errors for .ploy",
          "[polyls][diagnostics]") {
  Captured cap;
  PolylsServer s; MakeServer(s, cap);
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  s.HandleIncoming(MakeNotification("initialized", Json::object()));

  // Deliberate syntax error — unmatched brace.
  const Json open_params = Json{
      {"textDocument",
       {{"uri", "file:///bad.ploy"},
        {"languageId", "ploy"},
        {"version", 1},
        {"text", "FUNC broken( {\n"}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open_params));

  const Json *publish =
      FindNotification(cap, "textDocument/publishDiagnostics");
  REQUIRE(publish != nullptr);
  REQUIRE((*publish)["params"]["uri"] == "file:///bad.ploy");
  const Json &diags = (*publish)["params"]["diagnostics"];
  REQUIRE(diags.is_array());
  REQUIRE_FALSE(diags.empty());
  REQUIRE(diags[0]["source"] == "polyls");
  REQUIRE(diags[0]["severity"] == 1);  // Error
  REQUIRE(diags[0].contains("range"));
}

TEST_CASE("polyls shutdown then exit terminates cleanly",
          "[polyls][lifecycle]") {
  Captured cap;
  PolylsServer s; MakeServer(s, cap);
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));

  s.HandleIncoming(MakeRequest(2, "shutdown", Json()));
  REQUIRE(s.ShutdownRequested());
  const Json *resp = FindResponse(cap, 2);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("result"));

  // Post-shutdown requests must be rejected.
  s.HandleIncoming(MakeRequest(3, "shutdown", Json()));
  const Json *resp2 = FindResponse(cap, 3);
  REQUIRE(resp2 != nullptr);
  REQUIRE(resp2->contains("error"));

  s.HandleIncoming(MakeNotification("exit", Json()));
  REQUIRE(s.ExitRequested());
}
