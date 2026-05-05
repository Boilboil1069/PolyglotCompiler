/**
 * @file     inline_test_lens_test.cpp
 * @brief    Unit tests for `InlineTestLens`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/testing/inline_test_lens.h"

using namespace polyglot::tools::ui::testing;

TEST_CASE("InlineTestLens spots Catch2 TEST_CASE declarations",
          "[polyui][testing][lens]") {
  InlineTestLens lens;
  std::string code =
      "// header\n"
      "TEST_CASE(\"adds two numbers\", \"[math]\") {\n"
      "  CHECK(1 + 1 == 2);\n"
      "}\n";
  auto out = lens.ComputeForFile("foo.cpp", code);
  REQUIRE(out.size() == 1);
  CHECK(out[0].line == 2);
  CHECK(out[0].framework == "catch2");
  CHECK(out[0].symbol == "adds two numbers");
  CHECK(out[0].actions.size() == 2);
}

TEST_CASE("InlineTestLens recognises pytest functions",
          "[polyui][testing][lens]") {
  InlineTestLens lens;
  auto out = lens.ComputeForFile("test_foo.py",
                                 "def helper():\n    pass\n"
                                 "def test_addition():\n    assert 1 + 1 == 2\n");
  REQUIRE(out.size() == 1);
  CHECK(out[0].line == 3);
  CHECK(out[0].framework == "pytest");
  CHECK(out[0].symbol == "test_addition");
}

TEST_CASE("InlineTestLens recognises Rust #[test] functions",
          "[polyui][testing][lens]") {
  InlineTestLens lens;
  auto out = lens.ComputeForFile("lib.rs",
                                 "fn helper() {}\n"
                                 "#[test]\n"
                                 "fn it_works() { assert!(true); }\n");
  REQUIRE(out.size() == 1);
  CHECK(out[0].line == 3);
  CHECK(out[0].symbol == "it_works");
  CHECK(out[0].framework == "cargo");
}

TEST_CASE("InlineTestLens recognises JUnit @Test methods",
          "[polyui][testing][lens]") {
  InlineTestLens lens;
  auto out = lens.ComputeForFile("Foo.java",
                                 "@Test\npublic void shouldWork() {}\n");
  REQUIRE(out.size() == 1);
  CHECK(out[0].framework == "junit");
  CHECK(out[0].symbol == "shouldWork");
}

TEST_CASE("InlineTestLens distinguishes xUnit and NUnit attributes",
          "[polyui][testing][lens]") {
  InlineTestLens lens;
  auto x = lens.ComputeForFile("Foo.cs",
                               "[Fact]\npublic void Adds() {}\n");
  REQUIRE(x.size() == 1);
  CHECK(x[0].framework == "xunit");
  auto n = lens.ComputeForFile("Bar.cs",
                               "[Test]\npublic void Subtracts() {}\n");
  REQUIRE(n.size() == 1);
  CHECK(n[0].framework == "nunit");
}

TEST_CASE("RecordFailure attaches diagnostic to the cached lens",
          "[polyui][testing][lens]") {
  InlineTestLens lens;
  lens.ComputeForFile("foo.cpp", "TEST_CASE(\"x\") {}\n");
  CHECK(lens.RecordFailure("foo.cpp", 1, "expected 2 got 3"));
  CHECK_FALSE(lens.RecordFailure("foo.cpp", 99, "n/a"));
  auto cached = lens.Cached("foo.cpp");
  REQUIRE(cached.size() == 1);
  REQUIRE(cached[0].failure_message.has_value());
  CHECK(*cached[0].failure_message == "expected 2 got 3");
}
