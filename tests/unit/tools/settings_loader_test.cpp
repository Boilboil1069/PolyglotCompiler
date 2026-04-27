/**
 * @file     settings_loader_test.cpp
 * @brief    Unit tests for the shared 3-layer settings loader
 *
 * Covers:
 *   • Default / user / workspace merge precedence
 *   • DeepMerge and dotted-key get/set round-trips
 *   • Schema validation (type / enum / range)
 *   • EffectiveSettings diagnostics
 *
 * @ingroup  Tests / unit / tools
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "tools/common/include/effective_settings_loader.h"

namespace fs = std::filesystem;
using namespace polyglot::tools::common;
using nlohmann::json;

namespace {

constexpr const char *kDefaults = R"({
  "editor.tabSize": 4,
  "editor.insertSpaces": true,
  "workbench.colorTheme": "Polyglot Dark",
  "topology.layoutAlgorithm": "hierarchical"
})";

constexpr const char *kSchema = R"({
  "properties": {
    "editor.tabSize":            { "type": "integer", "minimum": 1, "maximum": 16 },
    "editor.insertSpaces":       { "type": "boolean" },
    "workbench.colorTheme":      { "type": "string"  },
    "topology.layoutAlgorithm":  { "type": "string", "enum":
                                    ["hierarchical","force_directed","grid","circular","layered"] }
  }
})";

fs::path WriteTempJson(const std::string &name, const std::string &body) {
  fs::path p = fs::temp_directory_path() / "polyglot_settings_test" / name;
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::trunc);
  f << body;
  return p;
}

}  // namespace

TEST_CASE("DeepMerge: deep object merge keeps shallow keys", "[settings_loader]") {
  json a = json::parse(R"({"a":1, "b":{"x":1, "y":2}})");
  json b = json::parse(R"({"b":{"y":99, "z":3}, "c":7})");
  DeepMerge(a, b);
  REQUIRE(a["a"] == 1);
  REQUIRE(a["b"]["x"] == 1);
  REQUIRE(a["b"]["y"] == 99);  // overridden
  REQUIRE(a["b"]["z"] == 3);
  REQUIRE(a["c"] == 7);
}

TEST_CASE("Dotted keys: flat round-trip", "[settings_loader]") {
  json tree = json::object();
  SetByDottedKey(tree, "editor.tabSize", json(8));
  SetByDottedKey(tree, "topology.layoutAlgorithm", json("grid"));
  REQUIRE(GetByDottedKey(tree, "editor.tabSize") == 8);
  REQUIRE(GetByDottedKey(tree, "topology.layoutAlgorithm") == "grid");
  REQUIRE(GetByDottedKey(tree, "missing.key").is_null());
}

TEST_CASE("Schema validation: enum + range + type", "[settings_loader]") {
  json data = json::parse(R"({
    "editor.tabSize": 999,
    "topology.layoutAlgorithm": "spaghetti",
    "editor.insertSpaces": "yes"
  })");
  std::vector<SettingsDiagnostic> diags;
  const bool ok = ValidateAgainstSchema(data, kSchema, &diags);
  REQUIRE_FALSE(ok);
  REQUIRE(diags.size() >= 3);
}

TEST_CASE("3-layer merge precedence (default<user<workspace)", "[settings_loader]") {
  const auto user_path = WriteTempJson(
      "user.json", R"({"editor.tabSize": 8, "workbench.colorTheme": "Light"})");
  const auto ws_path = WriteTempJson(
      "ws.json", R"({"editor.tabSize": 2})");

  auto eff = LoadEffectiveSettingsExplicit(kDefaults, kSchema, user_path, ws_path);
  REQUIRE(eff.effective["editor.tabSize"] == 2);          // workspace wins
  REQUIRE(eff.effective["workbench.colorTheme"] == "Light");  // user wins
  REQUIRE(eff.effective["editor.insertSpaces"] == true);   // default fallback
  REQUIRE(eff.diagnostics.empty());
}

TEST_CASE("Invalid user JSON yields a diagnostic, default still applies",
          "[settings_loader]") {
  const auto user_path = WriteTempJson("broken.json", R"({"editor.tabSize": 4)");  // missing brace
  auto eff = LoadEffectiveSettingsExplicit(kDefaults, kSchema, user_path, fs::path{});
  REQUIRE(eff.effective["editor.tabSize"] == 4);  // default
  REQUIRE_FALSE(eff.diagnostics.empty());
  REQUIRE(eff.diagnostics.front().scope == "user");
}

TEST_CASE("Path resolvers return platform-appropriate paths", "[settings_loader]") {
  const auto p = UserSettingsPath();
  REQUIRE_FALSE(p.empty());
  REQUIRE(p.filename() == "settings.json");
  const auto kb = UserKeybindingsPath();
  REQUIRE(kb.parent_path() == p.parent_path());
  REQUIRE(kb.filename() == "keybindings.json");
}
