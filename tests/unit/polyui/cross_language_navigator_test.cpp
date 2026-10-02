/**
 * @file     cross_language_navigator_test.cpp
 * @brief    Unit tests for `LinkRegistry` + `RenamePlanner`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/cross_language/cross_language_navigator.h"

using namespace polyglot::tools::ui::cross_language;

namespace {

LinkRegistry MakeRegistry() {
  LinkRegistry r;
  Definition d;
  d.language = HostLanguage::kCpp;
  d.symbol = "math::add";
  d.location = {"src/math.cpp", 12, 5};
  r.AddDefinition(d);

  LinkSite s1;
  s1.id = "poly-1";
  s1.target_language = HostLanguage::kCpp;
  s1.target_symbol = "math::add";
  s1.location = {"app.poly", 7, 3};
  r.AddSite(s1);

  LinkSite s2 = s1;
  s2.id = "poly-2";
  s2.location = {"app.poly", 19, 3};
  r.AddSite(s2);
  return r;
}

}  // namespace

TEST_CASE("HostLanguage name round-trips for all five hosts",
          "[polyui][crosslang][nav]") {
  for (auto h : {HostLanguage::kCpp, HostLanguage::kRust, HostLanguage::kPython,
                 HostLanguage::kJava, HostLanguage::kDotnet}) {
    CHECK(*HostLanguageFromName(HostLanguageName(h)) == h);
  }
  CHECK(*HostLanguageFromName("c++") == HostLanguage::kCpp);
  CHECK(*HostLanguageFromName("csharp") == HostLanguage::kDotnet);
  CHECK_FALSE(HostLanguageFromName("nope"));
}

TEST_CASE("GotoDefinition resolves LINK sites to host-language defs",
          "[polyui][crosslang][nav]") {
  auto r = MakeRegistry();
  auto site = r.sites().front();
  auto def = r.GotoDefinition(site);
  REQUIRE(def);
  CHECK(def->location.file == "src/math.cpp");
  CHECK(def->location.line == 12);
}

TEST_CASE("Reverse references and CodeLens count match",
          "[polyui][crosslang][nav]") {
  auto r = MakeRegistry();
  auto refs = r.FindLinkReferences(r.definitions().front());
  CHECK(refs.size() == 2);
  auto lenses = r.CodeLensFor("src/math.cpp");
  REQUIRE(lenses.size() == 1);
  CHECK(lenses[0].reference_count == 2);
  CHECK(lenses[0].symbol == "math::add");
}

TEST_CASE("RenamePlanner emits coordinated WorkspaceEdits",
          "[polyui][crosslang][nav]") {
  auto r = MakeRegistry();
  RenamePlanner p(r);
  Reference extra;
  extra.symbol = "math::add";
  extra.location = {"src/math.cpp", 30, 9};
  auto edits = p.Plan(HostLanguage::kCpp, "math::add", "math::sum", {extra});
  // 1 def + 2 poly sites + 1 extra reference.
  REQUIRE(edits.size() == 4);
  for (const auto &e : edits) CHECK(e.new_text == "math::sum");
  // length matches the old symbol length.
  CHECK(edits[0].length == static_cast<int>(std::string("math::add").size()));
}

TEST_CASE("Every host language exposes source docs and exact definition position", "[polyui][crosslang][docs]") {
  const std::vector<std::pair<std::string, std::string>> fixtures = {
      {"cpp", "/** Compute a score. */\nint score(int value) { return value; }\n"},
      {"python", "def score(value: int) -> int:\n    \"\"\"Compute a score.\"\"\"\n    return value\n"},
      {"rust", "/// Compute a score.\npub fn score(value: i64) -> i64 { value }\n"},
      {"go", "// Compute a score.\nfunc score(value int64) int64 { return value }\n"},
      {"java", "/** Compute a score. */\npublic static int score(int value) { return value; }\n"},
      {"csharp", "/// Compute a score.\npublic static int score(int value) { return value; }\n"},
      {"javascript", "/** Compute a score. */\nfunction score(value) { return value; }\n"},
      {"ruby", "# Compute a score.\ndef score(value)\n  value\nend\n"},
  };
  for (const auto &[language, source] : fixtures) {
    INFO(language);
    auto docs = ExtractFunctionDocumentation(source, language, "module.source");
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "score");
    CHECK(docs[0].documentation == "Compute a score.");
    CHECK(docs[0].signature.find("score(") != std::string::npos);
    CHECK(docs[0].source_preview.find("value") != std::string::npos);
    CHECK(docs[0].location.line == (language == "python" ? 1 : 2));
    CHECK(docs[0].location.column > 0);
  }
}

TEST_CASE("Cross-language target is resolved by source position with comments excluded", "[polyui][crosslang][docs]") {
  const std::string source =
      "// CALL(cpp, fake::score, 1)\n"
      "LET example = \"CALL(rust, fake::score, 1)\";\n"
      "LET result = CALL(\n  python, pricing::score, amount\n);\n"
      "LINK go::invoice::total;\n";
  CHECK_FALSE(ForeignTargetAt(source, source.find("fake::score")));
  CHECK_FALSE(ForeignTargetAt(source, source.rfind("fake::score")));
  const auto call = ForeignTargetAt(source, source.find("pricing::score") + 10);
  REQUIRE(call);
  CHECK(call->language == "python");
  CHECK(call->qualified_symbol == "pricing::score");
  const auto link = ForeignTargetAt(source, source.find("invoice::total") + 12);
  REQUIRE(link);
  CHECK(link->language == "go");
  CHECK(link->qualified_symbol == "invoice::total");
}

TEST_CASE("Foreign module paths do not confuse equally named functions", "[polyui][crosslang][docs]") {
  const auto paths = ForeignSourceCandidates("/workspace/app.poly", {"python", "pricing::score"});
  REQUIRE(paths.size() == 2);
  CHECK(paths[0] == "/workspace/pricing.py");
  CHECK(paths[1] == "/workspace/python/pricing.py");
  const auto overloads = ExtractFunctionDocumentation(
      "int score(int x) { return x; }\n"
      "double score(double x) { return x; }\n", "cpp", "pricing.cpp");
  REQUIRE(overloads.size() == 2);
  CHECK(overloads[0].location.line == 1);
  CHECK(overloads[1].location.line == 2);
}

TEST_CASE("Documentation extraction handles multiline comments and Python docstrings", "[polyui][crosslang][docs]") {
  const auto cpp = ExtractFunctionDocumentation(
      "/**\n * A measured quantity.\n * @param value Input count.\n */\n"
      "int score(\n int value) { return value; }\n", "cpp", "metrics.cpp");
  REQUIRE(cpp.size() == 1);
  CHECK(cpp[0].documentation == "A measured quantity.\n@param value Input count.");
  CHECK(cpp[0].signature == "int score( int value)");
  const auto python = ExtractFunctionDocumentation(
      "def score(value: int) -> int:\n"
      "    '''A measured quantity.\n    Returns the input count.\n    '''\n"
      "    return value\n", "python", "metrics.py");
  REQUIRE(python.size() == 1);
  CHECK(python[0].documentation == "A measured quantity.\nReturns the input count.");
  CHECK(ExtractFunctionDocumentation("// int fake(int x) {}\n", "cpp", "x.cpp").empty());
}

TEST_CASE("Call expressions never become source definitions", "[polyui][crosslang][docs]") {
  const auto docs = ExtractFunctionDocumentation(
      "int score(int x) { return x; }\n"
      "int main() {\n    return score(4);\n}\n", "cpp", "score.cpp");
  REQUIRE(docs.size() == 2);
  CHECK(docs[0].name == "score");
  CHECK(docs[1].name == "main");
}
