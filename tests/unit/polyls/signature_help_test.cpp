/**
 * @file     signature_help_test.cpp
 * @brief    Unit tests for polyls textDocument/signatureHelp
 *           (demand 2026-04-28-21 §3, §4)
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>

#include <string>
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

PolylsServer MakeReadyServer(Captured &cap) {
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  s.HandleIncoming(MakeNotification("initialized", Json::object()));
  cap.outbound.clear();
  return s;
}

const Json *FindResponse(const Captured &cap, int id) {
  for (const auto &p : cap.outbound)
    if (p.value("id", -1) == id) return &p;
  return nullptr;
}

}  // namespace

TEST_CASE("polyls signatureHelp returns matching FUNC and active parameter",
          "[polyls][signature-help]") {
  Captured cap;
  PolylsServer s = MakeReadyServer(cap);
  const Json open = Json{
      {"textDocument",
       {{"uri", "file:///s.ploy"},
        {"languageId", "ploy"},
        {"version", 1},
        {"text",
         "FUNC add(a, b) -> INT { RETURN a; }\n"
         "FUNC main() { add(1, )\n}\n"}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open));

  // Cursor sits between the comma and the closing paren on line 1.
  const Json params = Json{
      {"textDocument", {{"uri", "file:///s.ploy"}}},
      {"position", {{"line", 1}, {"character", 21}}}};
  s.HandleIncoming(MakeRequest(30, "textDocument/signatureHelp", params));

  const Json *resp = FindResponse(cap, 30);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("result"));
  REQUIRE_FALSE((*resp)["result"].is_null());
  const Json &sigs = (*resp)["result"]["signatures"];
  REQUIRE(sigs.is_array());
  REQUIRE_FALSE(sigs.empty());
  REQUIRE(sigs[0]["label"].get<std::string>().find("add") != std::string::npos);
  REQUIRE((*resp)["result"]["activeParameter"] == 1);
}

TEST_CASE("polyls signatureHelp returns null outside any call",
          "[polyls][signature-help]") {
  Captured cap;
  PolylsServer s = MakeReadyServer(cap);
  const Json open = Json{{"textDocument",
                          {{"uri", "file:///s2.ploy"},
                           {"languageId", "ploy"},
                           {"version", 1},
                           {"text", "LET x = 1\n"}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open));

  const Json params = Json{
      {"textDocument", {{"uri", "file:///s2.ploy"}}},
      {"position", {{"line", 0}, {"character", 5}}}};
  s.HandleIncoming(MakeRequest(31, "textDocument/signatureHelp", params));

  const Json *resp = FindResponse(cap, 31);
  REQUIRE(resp != nullptr);
  REQUIRE((*resp)["result"].is_null());
}
