/**
 * @file     folding_model_test.cpp
 * @brief    Boundary tests for `ComputeFolds`
 *           (demand 2026-04-28-26 §2).
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/editing/folding_model.h"

using polyglot::tools::ui::ComputeFolds;
using polyglot::tools::ui::FoldKind;

TEST_CASE("Brace block produces a fold", "[polyui][folding]") {
  auto folds = ComputeFolds(
      "FUNC f() {\n"
      "  RETURN 1\n"
      "}\n");
  REQUIRE(folds.size() == 1);
  REQUIRE(folds[0].kind == FoldKind::kBlock);
  REQUIRE(folds[0].start_line == 0);
  REQUIRE(folds[0].end_line == 2);
}

TEST_CASE("Region markers fold", "[polyui][folding]") {
  auto folds = ComputeFolds(
      "// region helpers\n"
      "let x = 1\n"
      "let y = 2\n"
      "// endregion\n");
  bool has_region = false;
  for (const auto &f : folds) {
    if (f.kind == FoldKind::kRegion && f.start_line == 0 && f.end_line == 3) {
      has_region = true;
    }
  }
  REQUIRE(has_region);
}

TEST_CASE("Multi-line block comment folds", "[polyui][folding]") {
  auto folds = ComputeFolds(
      "/* docs\n"
      "   line 2\n"
      "   end */\n"
      "code\n");
  bool has_comment = false;
  for (const auto &f : folds) {
    if (f.kind == FoldKind::kComment && f.start_line == 0 && f.end_line == 2) {
      has_comment = true;
    }
  }
  REQUIRE(has_comment);
}

TEST_CASE("Single-line block does not fold", "[polyui][folding]") {
  auto folds = ComputeFolds("FUNC f() { RETURN 1 }\n");
  REQUIRE(folds.empty());
}

TEST_CASE("Braces inside strings are ignored", "[polyui][folding]") {
  auto folds = ComputeFolds(
      "let s = \"a { b\"\n"
      "let t = \"c } d\"\n");
  REQUIRE(folds.empty());
}
