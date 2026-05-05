/**
 * @file     workspace_test.cpp
 * @brief    Unit tests for the multi-root workspace.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/workspace/workspace.h"

using namespace polyglot::tools::ui::workspace;

TEST_CASE("Parse / serialise polyui.code-workspace",
          "[polyui][workspace]") {
  std::string doc = R"({
    "folders":[
      {"name":"core","path":"/repo/core",
       "settings":{"polyls.formatOnSave":"true"}},
      {"name":"sdk","path":"/repo/sdk"}
    ],
    "settings":{"editor.tabSize":"2"}
  })";
  auto wf = ParseWorkspaceFile(doc);
  REQUIRE(wf);
  REQUIRE(wf->folders.size() == 2);
  CHECK(wf->folders[0].settings.at("polyls.formatOnSave") == "true");
  CHECK(wf->settings.at("editor.tabSize") == "2");

  auto round = SerializeWorkspaceFile(*wf);
  auto wf2 = ParseWorkspaceFile(round);
  REQUIRE(wf2);
  CHECK(wf2->folders.size() == 2);
  CHECK_FALSE(ParseWorkspaceFile("{}"));
}

TEST_CASE("AddRoot / RemoveRoot / FindRoot",
          "[polyui][workspace]") {
  Workspace w;
  WorkspaceFile wf;
  w.Load(wf);
  CHECK(w.AddRoot({"core", "/repo/core", {}}));
  CHECK(w.AddRoot({"sdk", "/repo/sdk", {}}));
  CHECK_FALSE(w.AddRoot({"core", "/repo/dup", {}}));
  CHECK_FALSE(w.AddRoot({"", "", {}}));
  CHECK(w.FindRoot("core") != nullptr);
  CHECK(w.FindRoot("missing") == nullptr);
  CHECK(w.RemoveRoot("sdk"));
  CHECK(w.roots().size() == 1);
}

TEST_CASE("EffectiveSetting falls back from folder to workspace",
          "[polyui][workspace]") {
  WorkspaceFile wf;
  wf.settings["editor.tabSize"] = "4";
  wf.settings["polyls.flag"] = "global";
  WorkspaceFolder a;
  a.name = "core";
  a.path = "/repo/core";
  a.settings["polyls.flag"] = "local";
  wf.folders.push_back(a);
  WorkspaceFolder b;
  b.name = "sdk";
  b.path = "/repo/sdk";
  wf.folders.push_back(b);
  Workspace w;
  w.Load(wf);

  CHECK(*w.EffectiveSetting("core", "polyls.flag") == "local");
  CHECK(*w.EffectiveSetting("sdk",  "polyls.flag") == "global");
  CHECK(*w.EffectiveSetting("core", "editor.tabSize") == "4");
  CHECK_FALSE(w.EffectiveSetting("core", "missing.key"));
}

TEST_CASE("ContainsPath + cross-root Search",
          "[polyui][workspace]") {
  Workspace w;
  w.AddRoot({"core", "/repo/core", {}});
  w.AddRoot({"sdk",  "/repo/sdk",  {}});
  std::string root;
  CHECK(w.ContainsPath("/repo/core/main.ploy", &root));
  CHECK(root == "core");
  CHECK_FALSE(w.ContainsPath("/elsewhere/x"));

  std::vector<std::pair<std::string, std::string>> index = {
      {"/repo/core/a.ploy",   "FN main() {}"},
      {"/repo/core/b.ploy",   "FN helper() {}"},
      {"/repo/sdk/api.ploy",  "FN api_main() {}"},
      {"/repo/sdk/log.ploy",  "FN log() {}"},
      {"/elsewhere/y.ploy",   "FN main() {}"},
  };
  auto hits = w.Search(index, "main");
  REQUIRE(hits.size() == 2);
  CHECK((hits[0].folder == "core" || hits[0].folder == "sdk"));
}

TEST_CASE("LanguageServerPool isolates per (folder,language,version)",
          "[polyui][workspace][lsp]") {
  LanguageServerPool pool;
  auto a = pool.Acquire({"core", "ploy", "1.0"});
  auto b = pool.Acquire({"core", "ploy", "1.0"});
  CHECK(a == b);
  auto c = pool.Acquire({"sdk", "ploy", "1.0"});
  CHECK(c != a);
  auto d = pool.Acquire({"core", "ploy", "2.0"});
  CHECK(d != a);
  CHECK(pool.size() == 3);
  CHECK(pool.Release({"core", "ploy", "1.0"}));
  CHECK_FALSE(pool.Release({"core", "ploy", "1.0"}));
  CHECK(pool.size() == 2);
}
