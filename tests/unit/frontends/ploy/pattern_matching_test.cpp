// ============================================================================
// Unit tests: extended MATCH pattern semantics (demand 2026-04-28-10)
//   - Wildcard `_` is irrefutable and silences exhaustiveness errors
//   - Range patterns `1..10` / `1..=10` parse and lower against integers
//   - OR patterns `1 | 2 | 3` parse and require uniform bindings
//   - Binding patterns `n @ 0..100` introduce the bound name in the body
//   - Type guards `x: i32 IF x > 0` accept a refined scrutinee
//   - OPTION Some/None constructor patterns drive Optional exhaustiveness
//   - Duplicate literal arms emit an unreachable-code warning
//   - Boolean MATCH without DEFAULT requires both TRUE and FALSE arms
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

namespace {

// Local helper mirroring the convention used by the other ploy unit tests
// (see typed_handle_test.cpp).  Returns diagnostics + the resulting module
// so each test case can assert on the messages without re-running the
// pipeline.
struct AnalyzeResult {
  Diagnostics diags;
  std::shared_ptr<polyglot::ploy::Module> module;
  std::unique_ptr<PloySema> sema;
  bool sema_ok{false};
};

AnalyzeResult AnalyzeSource(const std::string &src) {
  AnalyzeResult r;
  PloyLexer lexer(src, "<pattern_matching_test>");
  PloyParser parser(lexer, r.diags);
  parser.ParseModule();
  r.module = parser.TakeModule();
  if (!r.module) {
    return r;
  }
  r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
  r.sema_ok = r.sema->Analyze(r.module);
  return r;
}

} // namespace

// ----------------------------------------------------------------------------
// 1. Wildcard pattern is accepted as the universal default.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH wildcard pattern is irrefutable",
          "[ploy][sema][pattern_matching]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR x: i32 = 5
    MATCH x {
        CASE 0 -> { }
        CASE _ -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 2. Half-open and inclusive range patterns parse and type-check.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH range patterns accept numeric scrutinee",
          "[ploy][sema][pattern_matching]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR x: i32 = 7
    MATCH x {
        CASE 0..10 -> { }
        CASE 10..=20 -> { }
        CASE _ -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 3. OR pattern parses and the body sees the underlying scrutinee.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH OR pattern accepts multiple literal alternatives",
          "[ploy][sema][pattern_matching]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR x: i32 = 2
    MATCH x {
        CASE 1 | 2 | 3 -> { }
        CASE _ -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 4. Binding pattern `name @ subpattern` introduces the binding into the body.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH binding pattern introduces a bound name",
          "[ploy][sema][pattern_matching]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR x: i32 = 42
    MATCH x {
        CASE n @ 0..=100 -> { VAR y: i32 = n }
        CASE _ -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 5. Type guard binds a refined name and the IF clause uses it.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH type guard with IF refinement type-checks",
          "[ploy][sema][pattern_matching]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR x: i32 = 5
    MATCH x {
        CASE n: i32 IF n > 0 -> { }
        CASE _ -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 6. Boolean MATCH without DEFAULT requires both TRUE and FALSE arms.
// ----------------------------------------------------------------------------

TEST_CASE("Non-exhaustive boolean MATCH reports an error",
          "[ploy][sema][pattern_matching][exhaustiveness]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR flag: bool = TRUE
    MATCH flag {
        CASE TRUE -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  REQUIRE(r.diags.HasErrors());
}

TEST_CASE("Exhaustive boolean MATCH succeeds when both arms present",
          "[ploy][sema][pattern_matching][exhaustiveness]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR flag: bool = TRUE
    MATCH flag {
        CASE TRUE -> { }
        CASE FALSE -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 7. Duplicate literal arms emit an unreachable-code warning.
// ----------------------------------------------------------------------------

TEST_CASE("Duplicate literal arm emits unreachable warning",
          "[ploy][sema][pattern_matching][unreachable]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR x: i32 = 1
    MATCH x {
        CASE 1 -> { }
        CASE 1 -> { }
        CASE _ -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  // Sema still succeeds (warning, not error) — the unreachable arm is
  // accepted but flagged.  We only require the warning to be present
  // because exact code-string formatting is implementation-specific.
  CHECK(r.diags.HasWarnings());
}

// ----------------------------------------------------------------------------
// 8. Arms that follow an irrefutable wildcard are unreachable.
// ----------------------------------------------------------------------------

TEST_CASE("Arms following wildcard are flagged unreachable",
          "[ploy][sema][pattern_matching][unreachable]") {
  auto r = AnalyzeSource(R"PLOY(
PIPELINE main {
    VAR x: i32 = 1
    MATCH x {
        CASE _ -> { }
        CASE 5 -> { }
    }
}
)PLOY");
  REQUIRE(r.module);
  CHECK(r.diags.HasWarnings());
}
