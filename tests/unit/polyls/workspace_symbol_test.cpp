/**
 * @file     workspace_symbol_test.cpp
 * @brief    Boundary tests for `textDocument/documentSymbol` and
 *           `workspace/symbol` (demand 2026-04-28-25 §3).
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
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

const Json *FindResponse(const Captured &cap, int id) {
  for (const auto &p : cap.outbound) {
    if (p.value("id", -1) == id) return &p;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("polyls advertises documentSymbol + workspaceSymbol providers",
          "[polyls][symbols]") {
  Captured cap;
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });

  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  const Json *resp = FindResponse(cap, 1);
  REQUIRE(resp != nullptr);
  const Json &caps = (*resp)["result"]["capabilities"];
  REQUIRE(caps["documentSymbolProvider"] == true);
  REQUIRE(caps["workspaceSymbolProvider"] == true);
}

TEST_CASE("polyls documentSymbol returns ploy outline",
          "[polyls][symbols][documentSymbol]") {
  Captured cap;
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  cap.outbound.clear();

  const std::string text =
      "STRUCT Point {\n"
      "    x: INT\n"
      "    y: INT\n"
      "}\n"
      "FUNC area(p: Point) -> INT {\n"
      "    RETURN p.x * p.y\n"
      "}\n";
  Json open_params = {
      {"textDocument", {
          {"uri", "file:///tmp/sample.ploy"},
          {"languageId", "ploy"},
          {"version", 1},
          {"text", text},
      }},
  };
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open_params));
  cap.outbound.clear();

  Json req_params = {
      {"textDocument", {{"uri", "file:///tmp/sample.ploy"}}},
  };
  s.HandleIncoming(MakeRequest(2, "textDocument/documentSymbol", req_params));
  const Json *resp = FindResponse(cap, 2);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("result"));
  const Json &arr = (*resp)["result"];
  REQUIRE(arr.is_array());
  REQUIRE(arr.size() >= 2);
  bool found_area = false;
  for (const auto &sym : arr) {
    if (sym["name"] == "area") {
      found_area = true;
      REQUIRE(sym["kind"] == 12);  // Function
    }
  }
  REQUIRE(found_area);
}

TEST_CASE("polyls workspace/symbol filters by query substring",
          "[polyls][symbols][workspaceSymbol]") {
  Captured cap;
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  cap.outbound.clear();

  const std::string text =
      "FUNC compute_total(a: INT) -> INT { RETURN a }\n"
      "FUNC area(p: INT) -> INT { RETURN p }\n";
  Json open_params = {
      {"textDocument", {
          {"uri", "file:///tmp/sample.ploy"},
          {"languageId", "ploy"},
          {"version", 1},
          {"text", text},
      }},
  };
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open_params));
  cap.outbound.clear();

  s.HandleIncoming(MakeRequest(2, "workspace/symbol", Json{{"query", "tot"}}));
  const Json *resp = FindResponse(cap, 2);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("result"));
  const Json &arr = (*resp)["result"];
  REQUIRE(arr.is_array());
  bool found_total = false;
  bool found_area = false;
  for (const auto &sym : arr) {
    if (sym["name"] == "compute_total") found_total = true;
    if (sym["name"] == "area") found_area = true;
  }
  REQUIRE(found_total);
  REQUIRE_FALSE(found_area);
}
