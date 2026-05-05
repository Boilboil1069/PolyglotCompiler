/**
 * @file     semantic_tokens_test.cpp
 * @brief    LSP boundary tests for `textDocument/semanticTokens/full`
 *           and `textDocument/semanticTokens/range`
 *           (demand 2026-04-28-24).
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/ui/common/lsp/lsp_message.h"

namespace lsp = polyglot::tools::ui::lsp;
using polyglot::polyls::PolylsServer;
using Json = lsp::Json;

namespace {

struct Captured {
  std::vector<Json> outbound;
};

void MakeServer(PolylsServer &s, Captured &cap) {
  s.SetSendHandler([&cap](const Json &p) { cap.outbound.push_back(p); });
}

void Initialize(PolylsServer &s) {
  s.HandleIncoming(Json{{"jsonrpc", "2.0"},
                        {"id", 1},
                        {"method", "initialize"},
                        {"params", Json::object()}});
  s.HandleIncoming(Json{{"jsonrpc", "2.0"},
                        {"method", "initialized"},
                        {"params", Json::object()}});
}

void OpenPloy(PolylsServer &s, const std::string &uri,
              const std::string &text) {
  Json p = {
      {"textDocument",
       {{"uri", uri},
        {"languageId", "ploy"},
        {"version", 1},
        {"text", text}}}};
  s.HandleIncoming(Json{{"jsonrpc", "2.0"},
                        {"method", "textDocument/didOpen"},
                        {"params", p}});
}

}  // namespace

TEST_CASE("semanticTokens/full returns delta-encoded tokens for ploy",
          "[polyls][semantic]") {
  Captured cap;
  PolylsServer s;
  MakeServer(s, cap);
  Initialize(s);
  OpenPloy(s, "file:///t.ploy",
           "FUNC compute() -> INT { LET x = 3; RETURN x; }\n");
  cap.outbound.clear();
  s.HandleIncoming(Json{
      {"jsonrpc", "2.0"},
      {"id", 99},
      {"method", "textDocument/semanticTokens/full"},
      {"params",
       {{"textDocument", {{"uri", "file:///t.ploy"}}}}}});
  REQUIRE(!cap.outbound.empty());
  // Find the response that matches our id.
  bool found = false;
  for (const auto &msg : cap.outbound) {
    if (msg.contains("id") && msg["id"] == 99) {
      REQUIRE(msg.contains("result"));
      const auto &result = msg["result"];
      REQUIRE(result.contains("data"));
      REQUIRE(result["data"].is_array());
      REQUIRE(result["data"].size() % 5 == 0);
      REQUIRE(result["data"].size() >= 15);  // ≥3 tokens
      found = true;
    }
  }
  REQUIRE(found);
}

TEST_CASE("semanticTokens/full on unknown document yields null",
          "[polyls][semantic]") {
  Captured cap;
  PolylsServer s;
  MakeServer(s, cap);
  Initialize(s);
  cap.outbound.clear();
  s.HandleIncoming(Json{
      {"jsonrpc", "2.0"},
      {"id", 7},
      {"method", "textDocument/semanticTokens/full"},
      {"params",
       {{"textDocument", {{"uri", "file:///missing.ploy"}}}}}});
  bool ok = false;
  for (const auto &msg : cap.outbound) {
    if (msg.contains("id") && msg["id"] == 7) {
      REQUIRE(msg.contains("result"));
      REQUIRE(msg["result"].is_null());
      ok = true;
    }
  }
  REQUIRE(ok);
}

TEST_CASE("semanticTokens/range filters tokens by line window",
          "[polyls][semantic]") {
  Captured cap;
  PolylsServer s;
  MakeServer(s, cap);
  Initialize(s);
  OpenPloy(s, "file:///r.ploy",
           "FUNC a() -> VOID {}\n"
           "FUNC b() -> VOID {}\n"
           "FUNC c() -> VOID {}\n");
  cap.outbound.clear();
  s.HandleIncoming(Json{
      {"jsonrpc", "2.0"},
      {"id", 5},
      {"method", "textDocument/semanticTokens/range"},
      {"params",
       {{"textDocument", {{"uri", "file:///r.ploy"}}},
        {"range",
         {{"start", {{"line", 1}, {"character", 0}}},
          {"end", {{"line", 1}, {"character", 0}}}}}}}});
  bool ok = false;
  for (const auto &msg : cap.outbound) {
    if (msg.contains("id") && msg["id"] == 5) {
      REQUIRE(msg.contains("result"));
      const auto &result = msg["result"];
      REQUIRE(result["data"].is_array());
      // Window restricted to a single line should yield strictly
      // fewer tokens than the full pass would.
      REQUIRE(result["data"].size() < 75);
      ok = true;
    }
  }
  REQUIRE(ok);
}
