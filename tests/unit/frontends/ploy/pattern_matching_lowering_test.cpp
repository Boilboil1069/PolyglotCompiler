// ============================================================================
// pattern_matching_lowering_test.cpp — Lowering verification for the
// extended MATCH pattern semantics introduced by demand 2026-04-28-10.
//
// What we lock in here:
//   1. A MATCH whose arms are all simple integer literals (and no
//      guards) lowers to a single `ir::SwitchStatement` so the backend
//      can emit a dense jump table — the fast path documented in the
//      lowering pass.
//   2. A MATCH that contains a non-literal pattern (e.g. a range arm)
//      falls back to the structural cascade and never produces a
//      SwitchStatement.
//   3. The cascade path correctly materialises the match-merge join
//      block (every arm reaches it, the entry block is properly
//      terminated, and the IR module passes verification).
// ============================================================================

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/verifier.h"

using polyglot::frontends::Diagnostics;
using polyglot::ir::Function;
using polyglot::ir::IRContext;
using polyglot::ir::SwitchStatement;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyLowering;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

namespace {

struct LowerEnv {
  Diagnostics diags;
  IRContext ctx;
};

bool LowerSource(const std::string &code, LowerEnv &env) {
  PloyLexer lexer(code, "<pattern_matching_lowering_test>");
  PloyParser parser(lexer, env.diags);
  parser.ParseModule();
  auto module = parser.TakeModule();
  if (!module || env.diags.HasErrors())
    return false;

  PloySemaOptions opts;
  opts.enable_package_discovery = false;
  PloySema sema(env.diags, opts);
  if (!sema.Analyze(module))
    return false;

  PloyLowering lowering(env.ctx, env.diags, sema);
  return lowering.Lower(module);
}

// Returns the first SwitchStatement instruction found anywhere in `fn`,
// or nullptr when no switch was emitted.  The lowering pass walks each
// MATCH exactly once, so a non-null result for a single-MATCH function
// is unambiguous.
std::shared_ptr<SwitchStatement> FindSwitch(const Function &fn) {
  for (const auto &block : fn.blocks) {
    if (auto sw =
            std::dynamic_pointer_cast<SwitchStatement>(block->terminator)) {
      return sw;
    }
    for (const auto &inst : block->instructions) {
      if (auto sw = std::dynamic_pointer_cast<SwitchStatement>(inst))
        return sw;
    }
  }
  return nullptr;
}

bool AllBlocksTerminated(const Function &fn) {
  for (const auto &block : fn.blocks) {
    if (!block->terminator)
      return false;
  }
  return true;
}

} // namespace

// ----------------------------------------------------------------------------
// 1. Fast path: simple integer literal arms condense into a switch table.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH with integer literal arms lowers to a SwitchStatement",
          "[poly][lowering][pattern_matching][branch_table]") {
  LowerEnv env;
  REQUIRE(LowerSource(
      "FUNC pick(x: i32) -> i32 {\n"
      "  MATCH x {\n"
      "    CASE 0 { RETURN 100; }\n"
      "    CASE 1 { RETURN 101; }\n"
      "    CASE 2 { RETURN 102; }\n"
      "    CASE _ { RETURN 999; }\n"
      "  }\n"
      "  RETURN 0;\n"
      "}\n",
      env));
  REQUIRE_FALSE(env.diags.HasErrors());

  Function *fn = env.ctx.FindFunction("pick");
  REQUIRE(fn != nullptr);

  auto sw = FindSwitch(*fn);
  REQUIRE(sw != nullptr);
  // Three explicit literal arms => three switch cases.  The wildcard
  // arm becomes the default target rather than a numbered case.
  CHECK(sw->cases.size() == 3);
  REQUIRE(sw->default_target != nullptr);

  // Case values must match the source order so the backend's dense-table
  // optimisation sees a contiguous {0, 1, 2} domain.
  std::vector<long long> values;
  values.reserve(sw->cases.size());
  for (const auto &c : sw->cases) values.push_back(c.value);
  std::sort(values.begin(), values.end());
  CHECK(values == std::vector<long long>{0, 1, 2});

  REQUIRE(AllBlocksTerminated(*fn));
  std::string verify_msg;
  bool ok = polyglot::ir::Verify(env.ctx, &verify_msg);
  CAPTURE(verify_msg);
  CHECK(ok);
}

// ----------------------------------------------------------------------------
// 2. Cascade path: any non-literal arm forces structural lowering and
//    no SwitchStatement is emitted.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH with a range arm falls back to the structural cascade",
          "[poly][lowering][pattern_matching][branch_table]") {
  LowerEnv env;
  REQUIRE(LowerSource(
      "FUNC bucket(x: i32) -> i32 {\n"
      "  MATCH x {\n"
      "    CASE 0       { RETURN 0; }\n"
      "    CASE 1..=10  { RETURN 1; }\n"
      "    CASE _       { RETURN -1; }\n"
      "  }\n"
      "  RETURN 0;\n"
      "}\n",
      env));
  REQUIRE_FALSE(env.diags.HasErrors());

  Function *fn = env.ctx.FindFunction("bucket");
  REQUIRE(fn != nullptr);

  // Range pattern disqualifies the fast path.  The cascade lowering
  // uses CondBranchStatement chains instead, so no SwitchStatement
  // appears anywhere in the function body.
  CHECK(FindSwitch(*fn) == nullptr);

  REQUIRE(AllBlocksTerminated(*fn));
  std::string verify_msg;
  bool ok = polyglot::ir::Verify(env.ctx, &verify_msg);
  CAPTURE(verify_msg);
  CHECK(ok);
}

// ----------------------------------------------------------------------------
// 3. OPTION constructor patterns need tag and payload extraction. Until the
//    IR ABI exposes those operations, lowering must fail instead of comparing
//    the aggregate with an integer sentinel and binding the whole aggregate.
// ----------------------------------------------------------------------------

TEST_CASE("MATCH on OPTION fails closed without a tag and payload ABI",
          "[poly][lowering][pattern_matching][unsupported]") {
  LowerEnv env;
  CHECK_FALSE(LowerSource(
      "FUNC unwrap_or(opt: OPTION(i32), fallback: i32) -> i32 {\n"
      "  MATCH opt {\n"
      "    CASE Some(x) { RETURN x; }\n"
      "    CASE None    { RETURN fallback; }\n"
      "  }\n"
      "  RETURN fallback;\n"
      "}\n",
      env));
  CHECK(std::any_of(env.diags.All().begin(), env.diags.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
  }));
}

TEST_CASE("IF LET fails closed instead of using OPTION truthiness",
          "[poly][lowering][pattern_matching][unsupported]") {
  LowerEnv env;
  CHECK_FALSE(LowerSource(
      "FUNC use(opt: OPTION(i32)) -> i32 {\n"
      "  IF LET Some(x) = opt { RETURN x; } ELSE { RETURN 0; }\n"
      "}\n",
      env));
  CHECK(std::any_of(env.diags.All().begin(), env.diags.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
  }));
}

TEST_CASE("opaque cross-language result cannot become a branch condition",
          "[poly][lowering][abi][unsupported]") {
  LowerEnv env;
  CHECK_FALSE(LowerSource(
      "LINK(cpp, python, math::probe, util::probe) {\n"
      "  MAP_TYPE(cpp::int, python::int);\n"
      "}\n"
      "FUNC use() -> i32 {\n"
      "  IF CALL(cpp, math::probe, 1) { RETURN 1; }\n"
      "  RETURN 0;\n"
      "}\n",
      env));
  CHECK(std::any_of(env.diags.All().begin(), env.diags.All().end(), [](const auto &diag) {
    return diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
  }));
}
