/**
 * @file     formatting_test.cpp
 * @brief    Boundary tests for polyls formatting handlers
 *           (demand 2026-04-28-26 §3).
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

struct Captured { std::vector<Json> outbound; };

const Json *FindResponse(const Captured &cap, int id) {
  for (const auto &p : cap.outbound) {
    if (p.value("id", -1) == id) return &p;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("polyls advertises formatting providers",
          "[polyls][format]") {
  Captured cap;
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  const Json *resp = FindResponse(cap, 1);
  REQUIRE(resp != nullptr);
  const Json &caps = (*resp)["result"]["capabilities"];
  REQUIRE(caps["documentFormattingProvider"] == true);
  REQUIRE(caps["documentRangeFormattingProvider"] == true);
  REQUIRE(caps["documentOnTypeFormattingProvider"].is_object());
}

TEST_CASE("polyls textDocument/formatting reflows a ploy buffer",
          "[polyls][format]") {
  Captured cap;
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  cap.outbound.clear();

  const std::string text = "FUNC f() {\nRETURN 1\n}\n";
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
      {"options", {{"tabSize", 4}, {"insertSpaces", true}}},
  };
  s.HandleIncoming(MakeRequest(2, "textDocument/formatting", req_params));
  const Json *resp = FindResponse(cap, 2);
  REQUIRE(resp != nullptr);
  const Json &edits = (*resp)["result"];
  REQUIRE(edits.is_array());
  REQUIRE(edits.size() == 1);
  const std::string new_text = edits[0]["newText"].get<std::string>();
  REQUIRE(new_text.find("    RETURN 1") != std::string::npos);
}

TEST_CASE("polyls formatting on non-ploy buffer returns no edits",
          "[polyls][format]") {
  Captured cap;
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  cap.outbound.clear();

  Json open_params = {
      {"textDocument", {
          {"uri", "file:///tmp/foo.cpp"},
          {"languageId", "cpp"},
          {"version", 1},
          {"text", "int main(){}"},
      }},
  };
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open_params));
  cap.outbound.clear();

  Json req_params = {{"textDocument", {{"uri", "file:///tmp/foo.cpp"}}},
                     {"options", {{"tabSize", 2}, {"insertSpaces", true}}}};
  s.HandleIncoming(MakeRequest(2, "textDocument/formatting", req_params));
  const Json *resp = FindResponse(cap, 2);
  REQUIRE(resp != nullptr);
  REQUIRE((*resp)["result"].is_array());
  REQUIRE((*resp)["result"].empty());
}
