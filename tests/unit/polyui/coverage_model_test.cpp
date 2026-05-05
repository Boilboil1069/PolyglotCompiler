/**
 * @file     coverage_model_test.cpp
 * @brief    Unit tests for `CoverageModel` parsers.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/testing/coverage_model.h"

using namespace polyglot::tools::ui::testing;

TEST_CASE("DetectCoverageFormat sniffs the leading bytes",
          "[polyui][testing][coverage]") {
  CHECK(DetectCoverageFormat("TN:\nSF:foo.cpp\n") == CoverageFormat::kLcov);
  CHECK(DetectCoverageFormat("<?xml ?><coverage></coverage>") ==
        CoverageFormat::kCobertura);
  CHECK(DetectCoverageFormat("{ \"files\": [] }") ==
        CoverageFormat::kTarpaulin);
  CHECK(DetectCoverageFormat("{ \"Modules\": {} }") ==
        CoverageFormat::kCoverlet);
}

TEST_CASE("CoverageModel parses an lcov tracefile",
          "[polyui][testing][coverage]") {
  std::string lcov =
      "TN:\n"
      "SF:src/foo.cpp\n"
      "DA:1,3\n"
      "DA:2,0\n"
      "DA:3,7\n"
      "end_of_record\n";
  CoverageModel m;
  REQUIRE(m.Load(lcov));
  const auto *f = m.Find("src/foo.cpp");
  REQUIRE(f);
  CHECK(f->total_lines() == 3);
  CHECK(f->covered_lines() == 2);
  CHECK(f->percent() > 66.0);
}

TEST_CASE("CoverageModel parses Cobertura XML",
          "[polyui][testing][coverage]") {
  std::string xml = R"(<?xml version="1.0"?>
<coverage>
  <packages>
    <package>
      <classes>
        <class filename="src/a.py">
          <lines>
            <line number="1" hits="1"/>
            <line number="2" hits="0"/>
          </lines>
        </class>
      </classes>
    </package>
  </packages>
</coverage>)";
  CoverageModel m;
  REQUIRE(m.Load(xml));
  const auto *f = m.Find("src/a.py");
  REQUIRE(f);
  CHECK(f->covered_lines() == 1);
  CHECK(f->total_lines() == 2);
}

TEST_CASE("CoverageModel parses cargo-tarpaulin JSON",
          "[polyui][testing][coverage]") {
  std::string json = R"({
    "files": [
      {"path": ["src", "lib.rs"],
       "traces": [
         {"line": 1, "stats": {"Line": 4}},
         {"line": 2, "stats": {"Line": 0}}
       ]
      }
    ]
  })";
  CoverageModel m;
  REQUIRE(m.Load(json, CoverageFormat::kTarpaulin));
  const auto *f = m.Find("src/lib.rs");
  REQUIRE(f);
  CHECK(f->covered_lines() == 1);
  CHECK(f->total_lines() == 2);
}

TEST_CASE("CoverageModel parses coverlet JSON",
          "[polyui][testing][coverage]") {
  std::string json = R"({
    "Modules": {
      "Foo": {
        "Foo.Bar": {
          "Files": {
            "Foo.cs": {
              "Lines": { "10": 1, "11": 0, "12": 5 }
            }
          }
        }
      }
    }
  })";
  CoverageModel m;
  REQUIRE(m.Load(json, CoverageFormat::kCoverlet));
  const auto *f = m.Find("Foo.cs");
  REQUIRE(f);
  CHECK(f->covered_lines() == 2);
  CHECK(f->total_lines() == 3);
}

TEST_CASE("OverallPercent and BelowThreshold aggregate correctly",
          "[polyui][testing][coverage]") {
  CoverageModel m;
  m.Load("TN:\nSF:a\nDA:1,1\nDA:2,1\nend_of_record\n"
         "SF:b\nDA:1,0\nDA:2,0\nend_of_record\n");
  CHECK(m.Files().size() == 2);
  CHECK(m.OverallPercent() == 50.0);
  auto under = m.BelowThreshold(80.0);
  REQUIRE(under.size() == 1);
  CHECK(under[0]->file == "b");
}
