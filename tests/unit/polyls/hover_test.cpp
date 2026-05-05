/**
 * @file     hover_test.cpp
 * @brief    Unit tests for polyls textDocument/hover
 *           (demand 2026-04-28-21 §2, §4)
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

TEST_CASE("polyls hover renders Markdown for a user FUNC",
          "[polyls][hover]") {
  Captured cap;
  PolylsServer s = MakeReadyServer(cap);
  const Json open = Json{{"textDocument",
                          {{"uri", "file:///h.ploy"},
                           {"languageId", "ploy"},
                           {"version", 1},
                           {"text", "FUNC add(a, b) -> INT { RETURN a; }\n"}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open));

  const Json params = Json{
      {"textDocument", {{"uri", "file:///h.ploy"}}},
      {"position", {{"line", 0}, {"character", 6}}}};  // inside `add`
  s.HandleIncoming(MakeRequest(20, "textDocument/hover", params));

  const Json *resp = FindResponse(cap, 20);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("result"));
  REQUIRE_FALSE((*resp)["result"].is_null());
  const std::string md = (*resp)["result"]["contents"].value("value", std::string());
  REQUIRE(md.find("FUNC") != std::string::npos);
  REQUIRE(md.find("add") != std::string::npos);
}

TEST_CASE("polyls hover returns null for whitespace position",
          "[polyls][hover]") {
  Captured cap;
  PolylsServer s = MakeReadyServer(cap);
  const Json open = Json{{"textDocument",
                          {{"uri", "file:///h2.ploy"},
                           {"languageId", "ploy"},
                           {"version", 1},
                           {"text", "   \n"}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open));

  const Json params = Json{
      {"textDocument", {{"uri", "file:///h2.ploy"}}},
      {"position", {{"line", 0}, {"character", 1}}}};
  s.HandleIncoming(MakeRequest(21, "textDocument/hover", params));

  const Json *resp = FindResponse(cap, 21);
  REQUIRE(resp != nullptr);
  REQUIRE((*resp)["result"].is_null());
}
