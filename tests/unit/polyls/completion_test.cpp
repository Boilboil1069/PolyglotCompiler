/**
 * @file     completion_test.cpp
 * @brief    Unit tests for polyls textDocument/completion
 *           (demand 2026-04-28-21 §1, §4)
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

void MakeReadyServer(PolylsServer &s, Captured &cap) {
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  s.HandleIncoming(MakeNotification("initialized", Json::object()));
  cap.outbound.clear();
}

void Open(PolylsServer &s, const std::string &uri, const std::string &text) {
  const Json open = Json{{"textDocument",
                          {{"uri", uri},
                           {"languageId", "ploy"},
                           {"version", 1},
                           {"text", text}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open));
}

const Json *FindResponse(const Captured &cap, int id) {
  for (const auto &p : cap.outbound)
    if (p.value("id", -1) == id) return &p;
  return nullptr;
}

bool ContainsLabel(const Json &items, const std::string &label) {
  for (const auto &it : items)
    if (it.value("label", std::string()) == label) return true;
  return false;
}

}  // namespace

TEST_CASE("polyls completion emits .ploy keywords for a bare prefix",
          "[polyls][completion]") {
  Captured cap;
  PolylsServer s; MakeReadyServer(s, cap);
  Open(s, "file:///a.ploy", "FUN\n");

  const Json params = Json{
      {"textDocument", {{"uri", "file:///a.ploy"}}},
      {"position", {{"line", 0}, {"character", 3}}}};
  s.HandleIncoming(MakeRequest(10, "textDocument/completion", params));

  const Json *resp = FindResponse(cap, 10);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("result"));
  const Json &items = (*resp)["result"];
  REQUIRE(items.is_array());
  REQUIRE(ContainsLabel(items, "FUNC"));
}

TEST_CASE("polyls completion includes user-declared FUNC names",
          "[polyls][completion]") {
  Captured cap;
  PolylsServer s; MakeReadyServer(s, cap);
  Open(s, "file:///b.ploy",
       "FUNC compute_total(a, b) -> INT { RETURN a; }\n"
       "comp\n");

  const Json params = Json{
      {"textDocument", {{"uri", "file:///b.ploy"}}},
      {"position", {{"line", 1}, {"character", 4}}}};
  s.HandleIncoming(MakeRequest(11, "textDocument/completion", params));

  const Json *resp = FindResponse(cap, 11);
  REQUIRE(resp != nullptr);
  const Json &items = (*resp)["result"];
  REQUIRE(ContainsLabel(items, "compute_total"));
}

TEST_CASE("polyls completion offers cross-language template after `LINK cpp::`",
          "[polyls][completion][cross-language]") {
  Captured cap;
  PolylsServer s; MakeReadyServer(s, cap);
  Open(s, "file:///c.ploy", "LINK cpp::\n");

  const Json params = Json{
      {"textDocument", {{"uri", "file:///c.ploy"}}},
      {"position", {{"line", 0}, {"character", 10}}}};
  s.HandleIncoming(MakeRequest(12, "textDocument/completion", params));

  const Json *resp = FindResponse(cap, 12);
  REQUIRE(resp != nullptr);
  const Json &items = (*resp)["result"];
  REQUIRE(items.is_array());
  REQUIRE_FALSE(items.empty());
  // The first emitted item carries the cross-language module template.
  bool saw_module = false;
  for (const auto &it : items) {
    if (it.value("kind", 0) == 9) {  // Module
      saw_module = true;
      break;
    }
  }
  REQUIRE(saw_module);
}
