/**
 * @file     problem_matcher_test.cpp
 * @brief    Unit tests for the built-in problem matchers.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/tasks/problem_matcher.h"

using namespace polyglot::tools::ui::tasks;

TEST_CASE("$gcc matcher captures file/line/col/severity/message",
          "[polyui][tasks][matcher]") {
  ProblemMatcher m{"$gcc"};
  auto d = m.Match("src/foo.cpp:12:5: error: 'bar' was not declared");
  REQUIRE(d.has_value());
  CHECK(d->file == "src/foo.cpp");
  CHECK(d->line == 12);
  CHECK(d->column == 5);
  CHECK(d->severity == DiagnosticSeverity::kError);
  CHECK(d->message == "'bar' was not declared");
}

TEST_CASE("$msbuild matcher captures the code group",
          "[polyui][tasks][matcher]") {
  ProblemMatcher m{"$msbuild"};
  auto d = m.Match("C:\\src\\Foo.cs(7,3): error CS1002: ; expected");
  REQUIRE(d.has_value());
  CHECK(d->line == 7);
  CHECK(d->code == "CS1002");
}

TEST_CASE("$tsc matcher recognises TS error codes",
          "[polyui][tasks][matcher]") {
  ProblemMatcher m{"$tsc"};
  auto d = m.Match("src/x.ts(3,4): error TS2304: Cannot find name 'foo'.");
  REQUIRE(d.has_value());
  CHECK(d->code == "TS2304");
  CHECK(d->severity == DiagnosticSeverity::kError);
}

TEST_CASE("Matcher returns no diagnostic for unrelated lines",
          "[polyui][tasks][matcher]") {
  ProblemMatcher m;  // defaults to $gcc
  CHECK_FALSE(m.Match("Linking target foo").has_value());
}

TEST_CASE("DetectWatch flags begin / end lines",
          "[polyui][tasks][matcher]") {
  ProblemMatcher m{"$tsc"};
  CHECK(m.DetectWatch("Starting compilation in watch mode") ==
        WatchSignal::kBegin);
  CHECK(m.DetectWatch("Found 0 errors.") == WatchSignal::kEnd);
  CHECK(m.DetectWatch("just text") == WatchSignal::kNone);
}
