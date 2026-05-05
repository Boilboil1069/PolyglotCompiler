/**
 * @file     problems_panel_model_test.cpp
 * @brief    Unit tests for @ref polyglot::tools::ui::ProblemsAggregator
 *
 * Covers the data-model contract that drives the IDE Problems panel:
 * slice replacement, severity classification, snapshot ordering,
 * filtering (severity mask + file substring + source substring +
 * message regex), counts aggregation and the change-callback fanout.
 *
 * These tests are deliberately Qt-free so they run in any CI lane.
 *
 * @ingroup  Test / polyui / problems
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/include/problems_aggregator.h"

#include <atomic>

#include <catch2/catch_test_macros.hpp>

using polyglot::tools::ui::ClassifySeverity;
using polyglot::tools::ui::DiagnosticInfo;
using polyglot::tools::ui::ProblemEntry;
using polyglot::tools::ui::ProblemFilter;
using polyglot::tools::ui::ProblemsAggregator;
using polyglot::tools::ui::Severity;
using polyglot::tools::ui::SeverityMask;

namespace {

ProblemEntry MakeEntry(const std::string &file, const std::string &source,
                       Severity severity, std::size_t line,
                       std::string message, std::string code = {}) {
  ProblemEntry e;
  e.file = file;
  e.source = source;
  e.severity = severity;
  e.line = line;
  e.column = 1;
  e.end_line = line;
  e.end_column = 2;
  e.message = std::move(message);
  e.code = std::move(code);
  return e;
}

}  // namespace

TEST_CASE("ClassifySeverity maps the canonical labels", "[problems][severity]") {
  REQUIRE(ClassifySeverity("error")       == Severity::kError);
  REQUIRE(ClassifySeverity("Error")       == Severity::kError);
  REQUIRE(ClassifySeverity("warning")     == Severity::kWarning);
  REQUIRE(ClassifySeverity("warn")        == Severity::kWarning);
  REQUIRE(ClassifySeverity("info")        == Severity::kInformation);
  REQUIRE(ClassifySeverity("information") == Severity::kInformation);
  REQUIRE(ClassifySeverity("note")        == Severity::kHint);
  REQUIRE(ClassifySeverity("hint")        == Severity::kHint);
  // Unknown labels degrade to error (safer to surface than to swallow).
  REQUIRE(ClassifySeverity("")            == Severity::kError);
  REQUIRE(ClassifySeverity("garbled")     == Severity::kError);
}

TEST_CASE("Replace stores entries and Snapshot returns them sorted",
          "[problems][snapshot]") {
  ProblemsAggregator agg;
  agg.Replace("/work/b.cpp", "polyc", {
      MakeEntry("/work/b.cpp", "polyc", Severity::kError,   12, "boom B12"),
      MakeEntry("/work/b.cpp", "polyc", Severity::kWarning,  3, "tiny B3"),
  });
  agg.Replace("/work/a.cpp", "polyc", {
      MakeEntry("/work/a.cpp", "polyc", Severity::kError,    7, "boom A7"),
  });

  const auto snap = agg.Snapshot(ProblemFilter{});
  REQUIRE(snap.size() == 3);
  // Files sort lexicographically: a.cpp before b.cpp.
  REQUIRE(snap[0].file == "/work/a.cpp");
  REQUIRE(snap[1].file == "/work/b.cpp");
  REQUIRE(snap[2].file == "/work/b.cpp");
  // Within b.cpp, line 3 precedes line 12.
  REQUIRE(snap[1].line == 3);
  REQUIRE(snap[2].line == 12);
}

TEST_CASE("Replace with empty vector clears the slice", "[problems][replace]") {
  ProblemsAggregator agg;
  agg.Replace("/x.ploy", "polyls",
              {MakeEntry("/x.ploy", "polyls", Severity::kError, 1, "oops")});
  REQUIRE(agg.CountAll().Total() == 1);
  agg.Replace("/x.ploy", "polyls", {});
  REQUIRE(agg.CountAll().Total() == 0);
  REQUIRE(agg.KnownSources().empty());
}

TEST_CASE("ClearSource drops only that source across all files",
          "[problems][clear]") {
  ProblemsAggregator agg;
  agg.Replace("/a.cpp", "polyc",
              {MakeEntry("/a.cpp", "polyc", Severity::kError, 1, "x")});
  agg.Replace("/a.cpp", "polyls:cpp",
              {MakeEntry("/a.cpp", "polyls:cpp", Severity::kWarning, 2, "y")});
  agg.Replace("/b.cpp", "polyc",
              {MakeEntry("/b.cpp", "polyc", Severity::kError, 3, "z")});

  REQUIRE(agg.CountAll().Total() == 3);
  agg.ClearSource("polyc");
  REQUIRE(agg.CountAll().Total() == 1);
  const auto snap = agg.Snapshot(ProblemFilter{});
  REQUIRE(snap.size() == 1);
  REQUIRE(snap[0].source == "polyls:cpp");
}

TEST_CASE("ClearFile drops every entry for that path", "[problems][clear]") {
  ProblemsAggregator agg;
  agg.Replace("/a.cpp", "polyc",
              {MakeEntry("/a.cpp", "polyc", Severity::kError, 1, "x")});
  agg.Replace("/a.cpp", "polyls:cpp",
              {MakeEntry("/a.cpp", "polyls:cpp", Severity::kWarning, 2, "y")});
  agg.Replace("/b.cpp", "polyc",
              {MakeEntry("/b.cpp", "polyc", Severity::kError, 3, "z")});
  agg.ClearFile("/a.cpp");
  REQUIRE(agg.CountAll().Total() == 1);
  REQUIRE(agg.Snapshot(ProblemFilter{})[0].file == "/b.cpp");
}

TEST_CASE("Severity mask filters as expected", "[problems][filter]") {
  ProblemsAggregator agg;
  agg.Replace("/a", "s",
              {MakeEntry("/a", "s", Severity::kError,       1, "e"),
               MakeEntry("/a", "s", Severity::kWarning,     2, "w"),
               MakeEntry("/a", "s", Severity::kInformation, 3, "i"),
               MakeEntry("/a", "s", Severity::kHint,        4, "h")});

  ProblemFilter only_errors;
  only_errors.severity_mask = static_cast<std::uint32_t>(SeverityMask::kError);
  REQUIRE(agg.Snapshot(only_errors).size() == 1);

  ProblemFilter errors_and_warnings;
  errors_and_warnings.severity_mask =
      static_cast<std::uint32_t>(SeverityMask::kError) |
      static_cast<std::uint32_t>(SeverityMask::kWarning);
  REQUIRE(agg.Snapshot(errors_and_warnings).size() == 2);

  ProblemFilter none;
  none.severity_mask = 0;
  REQUIRE(agg.Snapshot(none).empty());
}

TEST_CASE("Substring filters are case-insensitive", "[problems][filter]") {
  ProblemsAggregator agg;
  agg.Replace("/work/Foo.cpp", "polyc",
              {MakeEntry("/work/Foo.cpp", "polyc", Severity::kError, 1, "alpha")});
  agg.Replace("/work/bar.ploy", "polyls",
              {MakeEntry("/work/bar.ploy", "polyls", Severity::kError, 1, "beta")});

  ProblemFilter f;
  f.file_substring = "FOO";
  REQUIRE(agg.Snapshot(f).size() == 1);
  REQUIRE(agg.Snapshot(f)[0].file == "/work/Foo.cpp");

  ProblemFilter g;
  g.source_substring = "LS";
  REQUIRE(agg.Snapshot(g).size() == 1);
  REQUIRE(agg.Snapshot(g)[0].source == "polyls");
}

TEST_CASE("Regex filter applied to message; invalid regex degrades silently",
          "[problems][filter]") {
  ProblemsAggregator agg;
  agg.Replace("/x", "s",
              {MakeEntry("/x", "s", Severity::kError, 1, "undefined symbol foo"),
               MakeEntry("/x", "s", Severity::kError, 2, "missing semicolon")});

  ProblemFilter undef;
  undef.message_regex = "undef.*sym";
  REQUIRE(agg.Snapshot(undef).size() == 1);

  ProblemFilter broken;
  broken.message_regex = "(unbalanced";  // invalid: missing close paren
  // Falls back to "no regex" → returns all entries.
  REQUIRE(agg.Snapshot(broken).size() == 2);
}

TEST_CASE("CountAll buckets by severity", "[problems][counts]") {
  ProblemsAggregator agg;
  agg.Replace("/a", "s",
              {MakeEntry("/a", "s", Severity::kError,       1, ""),
               MakeEntry("/a", "s", Severity::kError,       2, ""),
               MakeEntry("/a", "s", Severity::kWarning,     3, ""),
               MakeEntry("/a", "s", Severity::kInformation, 4, ""),
               MakeEntry("/a", "s", Severity::kHint,        5, "")});
  const auto c = agg.CountAll();
  REQUIRE(c.errors      == 2);
  REQUIRE(c.warnings    == 1);
  REQUIRE(c.information == 1);
  REQUIRE(c.hints       == 1);
  REQUIRE(c.Total()     == 5);
}

TEST_CASE("KnownSources is deduplicated and sorted", "[problems][sources]") {
  ProblemsAggregator agg;
  agg.Replace("/a", "polyc",
              {MakeEntry("/a", "polyc", Severity::kError, 1, "")});
  agg.Replace("/a", "polyls:cpp",
              {MakeEntry("/a", "polyls:cpp", Severity::kError, 1, "")});
  agg.Replace("/b", "polyc",
              {MakeEntry("/b", "polyc", Severity::kError, 1, "")});
  const auto srcs = agg.KnownSources();
  REQUIRE(srcs.size() == 2);
  REQUIRE(srcs[0] == "polyc");
  REQUIRE(srcs[1] == "polyls:cpp");
}

TEST_CASE("Change callback fires on mutation but not on detached state",
          "[problems][callback]") {
  ProblemsAggregator agg;
  std::atomic<int> ticks{0};
  agg.SetChangeCallback([&]() { ticks.fetch_add(1, std::memory_order_relaxed); });

  agg.Replace("/a", "s",
              {MakeEntry("/a", "s", Severity::kError, 1, "x")});
  agg.ClearSource("s");
  agg.Clear();
  REQUIRE(ticks.load() >= 3);

  agg.SetChangeCallback({});
  const int previous = ticks.load();
  agg.Replace("/b", "s",
              {MakeEntry("/b", "s", Severity::kError, 1, "y")});
  REQUIRE(ticks.load() == previous);
}

TEST_CASE("ReplaceFromDiagnosticInfo converts UI struct to ProblemEntry",
          "[problems][diagnostic_info]") {
  ProblemsAggregator agg;
  std::vector<DiagnosticInfo> diags;
  DiagnosticInfo d;
  d.line = 10;
  d.column = 4;
  d.end_line = 10;
  d.end_column = 12;
  d.severity = "warning";
  d.message = "unused variable";
  d.code = "W2001";
  d.suggestion = "delete the binding";
  diags.push_back(d);

  agg.ReplaceFromDiagnosticInfo("/a.ploy", "polyls:ploy", diags);
  const auto snap = agg.Snapshot(ProblemFilter{});
  REQUIRE(snap.size() == 1);
  REQUIRE(snap[0].severity == Severity::kWarning);
  REQUIRE(snap[0].line == 10);
  REQUIRE(snap[0].column == 4);
  REQUIRE(snap[0].end_column == 12);
  REQUIRE(snap[0].code == "W2001");
  REQUIRE(snap[0].suggestion == "delete the binding");
  REQUIRE(snap[0].file == "/a.ploy");
  REQUIRE(snap[0].source == "polyls:ploy");
}
