/**
 * @file     refactor_test.cpp
 * @brief    Unit tests for polyls refactoring engine
 *           (demand 2026-04-28-23)
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "tools/polyls/polyls_core/polyls_server.h"
#include "tools/polyls/polyls_core/refactor.h"
#include "tools/polyls/polyls_core/symbol_index.h"
#include "tools/ui/common/lsp/lsp_message.h"

using polyglot::polyls::BuildCodeActions;
using polyglot::polyls::BuildRenameEdit;
using polyglot::polyls::DocumentView;
using polyglot::polyls::IsValidIdentifier;
using polyglot::polyls::PolylsServer;
using polyglot::polyls::PrepareRename;
using polyglot::polyls::SymbolIndex;
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

TEST_CASE("IsValidIdentifier rejects keywords and bad shapes",
          "[polyls][refactor][rename]") {
  REQUIRE(IsValidIdentifier("foo"));
  REQUIRE(IsValidIdentifier("_bar1"));
  REQUIRE_FALSE(IsValidIdentifier(""));
  REQUIRE_FALSE(IsValidIdentifier("1abc"));
  REQUIRE_FALSE(IsValidIdentifier("with-dash"));
  REQUIRE_FALSE(IsValidIdentifier("FUNC"));
  REQUIRE_FALSE(IsValidIdentifier("class"));
}

TEST_CASE("PrepareRename returns identifier range under the cursor",
          "[polyls][refactor][rename]") {
  std::vector<DocumentView> docs = {
      {"file:///w/a.ploy", "ploy",
       "FUNC compute(a: INT) -> INT { RETURN a }\n"}};
  auto r = PrepareRename(docs, "file:///w/a.ploy", 0, /*char=*/7);
  REQUIRE(r.has_value());
  REQUIRE(r->start.line == 0);
  REQUIRE(r->start.character == 5);
  REQUIRE(r->end.character == 12);
}

TEST_CASE("PrepareRename refuses keywords / whitespace",
          "[polyls][refactor][rename]") {
  std::vector<DocumentView> docs = {
      {"file:///w/a.ploy", "ploy", "FUNC foo() -> INT { RETURN 1 }\n"}};
  REQUIRE_FALSE(
      PrepareRename(docs, "file:///w/a.ploy", 0, 1).has_value());  // FUNC
  REQUIRE_FALSE(
      PrepareRename(docs, "file:///w/a.ploy", 0, 11).has_value());  // space
}

TEST_CASE("BuildRenameEdit rewrites every reference in the open buffer",
          "[polyls][refactor][rename]") {
  SymbolIndex idx;
  const std::string uri = "file:///w/a.ploy";
  const std::string text =
      "FUNC compute(a: INT) -> INT { RETURN a }\n"
      "LET total: INT = compute(1)\n"
      "LET other: INT = compute(2)\n";
  idx.IndexDocument(uri, "ploy", text);
  std::vector<DocumentView> docs = {{uri, "ploy", text}};

  auto edit = BuildRenameEdit(idx, docs, uri, /*line=*/0, /*char=*/7,
                              "evaluate");
  REQUIRE(edit.has_value());
  auto it = edit->changes.find(uri);
  REQUIRE(it != edit->changes.end());
  // Definition + 2 call sites = 3 edits, all renamed to `evaluate`.
  REQUIRE(it->second.size() == 3);
  for (const auto &te : it->second) {
    REQUIRE(te.new_text == "evaluate");
  }
}

TEST_CASE("BuildRenameEdit ignores identifier substrings inside strings",
          "[polyls][refactor][rename]") {
  SymbolIndex idx;
  const std::string uri = "file:///w/a.ploy";
  const std::string text =
      "FUNC tag() -> STRING { RETURN \"tag is safe\" }\n"
      "LET v: STRING = tag()\n";
  idx.IndexDocument(uri, "ploy", text);
  std::vector<DocumentView> docs = {{uri, "ploy", text}};
  auto edit = BuildRenameEdit(idx, docs, uri, 0, 6, "label");
  REQUIRE(edit.has_value());
  // 1 definition + 1 call site = 2; the "tag" inside the string literal
  // must NOT be renamed.
  REQUIRE(edit->changes[uri].size() == 2);
}

TEST_CASE("BuildRenameEdit rejects invalid new names",
          "[polyls][refactor][rename]") {
  SymbolIndex idx;
  const std::string uri = "file:///w/a.ploy";
  const std::string text = "FUNC foo() -> INT { RETURN 1 }\n";
  idx.IndexDocument(uri, "ploy", text);
  std::vector<DocumentView> docs = {{uri, "ploy", text}};
  REQUIRE_FALSE(BuildRenameEdit(idx, docs, uri, 0, 6, "").has_value());
  REQUIRE_FALSE(BuildRenameEdit(idx, docs, uri, 0, 6, "1bad").has_value());
}

TEST_CASE("BuildRenameEdit propagates across .ploy LINK reverse refs",
          "[polyls][refactor][rename][cross]") {
  SymbolIndex idx;
  const std::string ploy_uri = "file:///w/main.ploy";
  const std::string ploy_text =
      "IMPORT cpp::image_processor;\n"
      "LINK cpp::image_processor::enhance AS FUNC(double) -> double;\n";
  const std::string cpp_uri = "file:///w/image_processor.cpp";
  const std::string cpp_text =
      "namespace image_processor {\n"
      "  void enhance(double x) {}\n"
      "  void other(double x) { enhance(x); }\n"
      "}\n";
  idx.IndexDocument(ploy_uri, "ploy", ploy_text);
  idx.IndexDocument(cpp_uri, "cpp", cpp_text);
  std::vector<DocumentView> docs = {
      {ploy_uri, "ploy", ploy_text},
      {cpp_uri, "cpp", cpp_text},
  };

  // Initiate rename from the cpp side on the bare identifier `enhance`.
  // Line 1, character 7 lands inside `enhance`.
  auto edit =
      BuildRenameEdit(idx, docs, cpp_uri, /*line=*/1, /*char=*/9, "boost");
  REQUIRE(edit.has_value());
  // C++ buffer: definition + intra-file call = 2 edits.
  REQUIRE(edit->changes[cpp_uri].size() == 2);
  // .ploy buffer: at least the LINK qualifier site rewritten.
  REQUIRE(edit->changes.count(ploy_uri) == 1);
  REQUIRE_FALSE(edit->changes[ploy_uri].empty());
}

TEST_CASE("BuildCodeActions surfaces extract / inline / change-sig / move",
          "[polyls][refactor][codeAction]") {
  SymbolIndex idx;
  const std::string uri = "file:///w/a.ploy";
  const std::string text =
      "FUNC foo() -> INT {\n"
      "    LET x: INT = 1 + 2\n"
      "    RETURN x\n"
      "}\n";
  idx.IndexDocument(uri, "ploy", text);
  std::vector<DocumentView> docs = {{uri, "ploy", text}};

  polyglot::tools::ui::lsp::Range r;
  r.start.line = 1;
  r.start.character = 0;
  r.end.line = 1;
  r.end.character = 22;

  auto actions = BuildCodeActions(idx, docs, uri, r, "extracted");
  // Extract + inline + inline-fn + change-sig + move = 5.
  REQUIRE(actions.size() >= 5);

  bool saw_extract = false, saw_inline_var = false, saw_change_sig = false,
       saw_move = false, saw_inline_fn = false;
  for (const auto &ca : actions) {
    if (ca.kind == "refactor.extract.function") saw_extract = true;
    if (ca.kind == "refactor.inline.variable") saw_inline_var = true;
    if (ca.kind == "refactor.inline.function") saw_inline_fn = true;
    if (ca.kind == "refactor.changeSignature") saw_change_sig = true;
    if (ca.kind == "refactor.move.file") saw_move = true;
  }
  REQUIRE(saw_extract);
  REQUIRE(saw_inline_var);
  REQUIRE(saw_inline_fn);
  REQUIRE(saw_change_sig);
  REQUIRE(saw_move);
}

TEST_CASE("polyls dispatches prepareRename / rename / codeAction",
          "[polyls][refactor][lsp]") {
  Captured cap;
  PolylsServer s;
  MakeReadyServer(s, cap);
  Open(s, "file:///w/a.ploy", "ploy",
       "FUNC compute(a: INT) -> INT { RETURN a }\n"
       "LET total: INT = compute(1)\n");

  // prepareRename
  s.HandleIncoming(MakeRequest(
      10, "textDocument/prepareRename",
      Json{{"textDocument", {{"uri", "file:///w/a.ploy"}}},
           {"position", Position(0, 7)}}));
  const Json *prep = FindResponse(cap, 10);
  REQUIRE(prep != nullptr);
  REQUIRE((*prep)["result"].contains("start"));

  // rename
  s.HandleIncoming(MakeRequest(
      11, "textDocument/rename",
      Json{{"textDocument", {{"uri", "file:///w/a.ploy"}}},
           {"position", Position(0, 7)},
           {"newName", "evaluate"}}));
  const Json *ren = FindResponse(cap, 11);
  REQUIRE(ren != nullptr);
  const auto &changes = (*ren)["result"]["changes"];
  REQUIRE(changes.contains("file:///w/a.ploy"));
  REQUIRE(changes["file:///w/a.ploy"].size() == 2);

  // codeAction
  s.HandleIncoming(MakeRequest(
      12, "textDocument/codeAction",
      Json{{"textDocument", {{"uri", "file:///w/a.ploy"}}},
           {"range",
            {{"start", Position(0, 0)}, {"end", Position(0, 30)}}}}));
  const Json *ca = FindResponse(cap, 12);
  REQUIRE(ca != nullptr);
  REQUIRE((*ca)["result"].is_array());
  REQUIRE((*ca)["result"].size() >= 1);
}

TEST_CASE("polyls rename rejects invalid newName at LSP boundary",
          "[polyls][refactor][lsp]") {
  Captured cap;
  PolylsServer s;
  MakeReadyServer(s, cap);
  Open(s, "file:///w/a.ploy", "ploy", "FUNC foo() -> INT { RETURN 1 }\n");
  s.HandleIncoming(MakeRequest(
      20, "textDocument/rename",
      Json{{"textDocument", {{"uri", "file:///w/a.ploy"}}},
           {"position", Position(0, 6)},
           {"newName", "1bad"}}));
  const Json *resp = FindResponse(cap, 20);
  REQUIRE(resp != nullptr);
  REQUIRE(resp->contains("error"));
}
