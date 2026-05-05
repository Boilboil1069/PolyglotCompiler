/**
 * @file     sample_browser_test.cpp
 * @brief    Unit tests for `SampleCatalogue`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/samples/sample_browser.h"

using namespace polyglot::tools::ui::samples;

TEST_CASE("EntryKind / Difficulty round-trip", "[polyui][samples]") {
  CHECK(*EntryKindFromName(EntryKindName(EntryKind::kSample)) ==
        EntryKind::kSample);
  CHECK(*EntryKindFromName(EntryKindName(EntryKind::kTutorial)) ==
        EntryKind::kTutorial);
  for (auto d : {Difficulty::kBeginner, Difficulty::kIntermediate,
                 Difficulty::kAdvanced})
    CHECK(*DifficultyFromName(DifficultyName(d)) == d);
  CHECK_FALSE(EntryKindFromName("nope"));
  CHECK_FALSE(DifficultyFromName("nope"));
}

TEST_CASE("LoadIndex parses a mixed sample/tutorial catalogue",
          "[polyui][samples]") {
  std::string idx = R"({
    "entries": [
      {"id":"hello","title":"Hello PolyGlot","kind":"sample",
       "difficulty":"beginner","languages":["cpp"],"topics":["intro"],
       "root_path":"tests/samples/hello","files":["main.ploy"],
       "summary":"prints hello"},
      {"id":"torch","title":"PyTorch interop","kind":"tutorial",
       "difficulty":"advanced","languages":["python","cpp"],
       "topics":["ai","ml"],"root_path":"docs/tutorial/torch",
       "files":["README.md","driver.ploy","model.py"],
       "summary":"call torch.nn from PolyGlot"}
    ]
  })";
  SampleCatalogue cat;
  REQUIRE(cat.LoadIndex(idx));
  REQUIRE(cat.entries().size() == 2);
  const auto *t = cat.Find("torch");
  REQUIRE(t);
  CHECK(t->kind == EntryKind::kTutorial);
  CHECK(t->difficulty == Difficulty::kAdvanced);
  CHECK(t->files.size() == 3);
  CHECK(t->topics.size() == 2);
}

TEST_CASE("Filter respects language / topic / difficulty / text",
          "[polyui][samples]") {
  SampleCatalogue cat;
  CatalogueEntry a{"a","Hello",EntryKind::kSample,Difficulty::kBeginner,
                   {"cpp"},{"intro"},"r",{"main.ploy"},"hello world"};
  CatalogueEntry b{"b","Torch",EntryKind::kTutorial,Difficulty::kAdvanced,
                   {"python","cpp"},{"ai","ml"},"r",{"m.ploy"},"pytorch"};
  cat.AddEntry(a); cat.AddEntry(b);

  CatalogueQuery q;
  q.languages = {"python"};
  auto py = cat.Filter(q);
  REQUIRE(py.size() == 1);
  CHECK(py[0]->id == "b");

  CatalogueQuery q2;
  q2.kind = EntryKind::kSample;
  q2.difficulty = Difficulty::kBeginner;
  q2.text = "hello";
  auto matches = cat.Filter(q2);
  REQUIRE(matches.size() == 1);
  CHECK(matches[0]->id == "a");

  CatalogueQuery q3;
  q3.topics = {"ai", "ml"};
  CHECK(cat.Filter(q3).size() == 1);
}

TEST_CASE("PlanCopy maps every file to the destination root",
          "[polyui][samples]") {
  SampleCatalogue cat;
  CatalogueEntry a{"k","K",EntryKind::kSample,Difficulty::kBeginner,
                   {"cpp"},{},"tests/samples/k",{"main.ploy","README.md"},""};
  cat.AddEntry(a);
  auto plan = cat.PlanCopy("k", "/tmp/dst");
  REQUIRE(plan);
  CHECK(plan->entry_id == "k");
  CHECK(plan->files.size() == 2);
  CHECK(plan->files[0].first == "tests/samples/k/main.ploy");
  CHECK(plan->files[0].second == "/tmp/dst/main.ploy");
  CHECK(plan->files[1].first == "tests/samples/k/README.md");
  CHECK_FALSE(cat.PlanCopy("missing", "/tmp/x"));
}
