/**
 * @file     navigation_test.cpp
 * @brief    Unit tests for polyls navigation handlers
 *           (demand 2026-04-28-22)
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
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

void Open(PolylsServer &s, const std::string &uri, const std::string &lang,
          const std::string &text) {
  const Json open = Json{{"textDocument",
                          {{"uri", uri},
                           {"languageId", lang},
                           {"version", 1},
                           {"text", text}}}};
  s.HandleIncoming(MakeNotification("textDocument/didOpen", open));
}

const Json *FindResponse(const Captured &cap, int id) {
  for (const auto &p : cap.outbound)
    if (p.value("id", -1) == id) return &p;
  return nullptr;
}

Json Position(int line, int ch) {
  return Json{{"line", line}, {"character", ch}};
}

}  // namespace

TEST_CASE("polyls advertises navigation capabilities",
          "[polyls][navigation][lifecycle]") {
  Captured cap;
  PolylsServer s;
  s.SetSendHandler([&](const Json &p) { cap.outbound.push_back(p); });
  s.HandleIncoming(MakeRequest(1, "initialize", Json::object()));
  const Json *resp = FindResponse(cap, 1);
  REQUIRE(resp != nullptr);
  const Json &caps = (*resp)["result"]["capabilities"];
  REQUIRE(caps.value("definitionProvider", false));
  REQUIRE(caps.value("declarationProvider", false));
  REQUIRE(caps.value("implementationProvider", false));
  REQUIRE(caps.value("typeDefinitionProvider", false));
  REQUIRE(caps.value("referencesProvider", false));
}

TEST_CASE("polyls definition jumps to FUNC declaration",
          "[polyls][navigation][definition]") {
  Captured cap;
  PolylsServer s; MakeReadyServer(s, cap);
  Open(s, "file:///w/main.ploy", "ploy",
       "FUNC compute(a: INT) -> INT { RETURN a }\n"
       "LET total: INT = compute(1)\n");

  const Json params = Json{
      {"textDocument", {{"uri", "file:///w/main.ploy"}}},
      {"position", Position(1, 17)}};  // cursor inside "compute"
  s.HandleIncoming(MakeRequest(10, "textDocument/definition", params));

  const Json *resp = FindResponse(cap, 10);
  REQUIRE(resp != nullptr);
  const Json &result = (*resp)["result"];
  REQUIRE(result.is_array());
  REQUIRE_FALSE(result.empty());
  REQUIRE(result[0]["uri"] == "file:///w/main.ploy");
  REQUIRE(result[0]["range"]["start"]["line"] == 0);
}

TEST_CASE("polyls references include every use of a name",
          "[polyls][navigation][references]") {
  Captured cap;
  PolylsServer s; MakeReadyServer(s, cap);
  Open(s, "file:///w/r.ploy", "ploy",
       "FUNC tally(x: INT) -> INT { RETURN x }\n"
       "LET a = tally(1)\n"
       "LET b = tally(2)\n");

  const Json params = Json{
      {"textDocument", {{"uri", "file:///w/r.ploy"}}},
      {"position", Position(1, 9)},
      {"context", {{"includeDeclaration", true}}}};
  s.HandleIncoming(MakeRequest(11, "textDocument/references", params));

  const Json *resp = FindResponse(cap, 11);
  REQUIRE(resp != nullptr);
  const Json &result = (*resp)["result"];
  REQUIRE(result.is_array());
  REQUIRE(result.size() >= 3);  // declaration + two call sites
}

TEST_CASE("polyls definition follows .ploy LINK across languages",
          "[polyls][navigation][cross-language]") {
  Captured cap;
  PolylsServer s; MakeReadyServer(s, cap);

  // Host C++ file (must be opened first so its symbols are indexed).
  Open(s, "file:///w/image_processor.cpp", "cpp",
       "namespace image_processor {\n"
       "void enhance(int x) { (void)x; }\n"
       "}\n");

  // .ploy LINK declaration (legacy form so the qualifier sits on a
  // single line and can be navigated by clicking on the cpp:: prefix).
  Open(s, "file:///w/pipe.ploy", "ploy",
       "LINK cpp::image_processor::enhance AS ploy_enhance\n");

  // Cursor on "enhance" in the LINK qualifier.
  const Json params = Json{
      {"textDocument", {{"uri", "file:///w/pipe.ploy"}}},
      {"position", Position(0, 28)}};
  s.HandleIncoming(MakeRequest(12, "textDocument/definition", params));

  const Json *resp = FindResponse(cap, 12);
  REQUIRE(resp != nullptr);
  const Json &result = (*resp)["result"];
  REQUIRE(result.is_array());
  REQUIRE_FALSE(result.empty());
  bool saw_cpp = false;
  for (const auto &loc : result) {
    if (loc.value("uri", std::string()) == "file:///w/image_processor.cpp") {
      saw_cpp = true;
      break;
    }
  }
  REQUIRE(saw_cpp);
}

TEST_CASE("polyls implementation on a LINK lands in the host language",
          "[polyls][navigation][implementation]") {
  Captured cap;
  PolylsServer s; MakeReadyServer(s, cap);
  Open(s, "file:///w/host.cpp", "cpp", "void worker() {}\n");
  Open(s, "file:///w/p.ploy", "ploy",
       "LINK cpp::worker AS ploy_worker\n");

  const Json params = Json{
      {"textDocument", {{"uri", "file:///w/p.ploy"}}},
      {"position", Position(0, 11)}};  // inside "worker"
  s.HandleIncoming(MakeRequest(13, "textDocument/implementation", params));

  const Json *resp = FindResponse(cap, 13);
  REQUIRE(resp != nullptr);
  const Json &result = (*resp)["result"];
  REQUIRE(result.is_array());
  REQUIRE_FALSE(result.empty());
}
