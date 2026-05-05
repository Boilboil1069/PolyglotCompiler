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
  s1.id = "ploy-1";
  s1.target_language = HostLanguage::kCpp;
  s1.target_symbol = "math::add";
  s1.location = {"app.ploy", 7, 3};
  r.AddSite(s1);

  LinkSite s2 = s1;
  s2.id = "ploy-2";
  s2.location = {"app.ploy", 19, 3};
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
  // 1 def + 2 ploy sites + 1 extra reference.
  REQUIRE(edits.size() == 4);
  for (const auto &e : edits) CHECK(e.new_text == "math::sum");
  // length matches the old symbol length.
  CHECK(edits[0].length == static_cast<int>(std::string("math::add").size()));
}
