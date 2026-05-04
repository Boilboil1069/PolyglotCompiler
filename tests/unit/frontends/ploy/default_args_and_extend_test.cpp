// ============================================================================
// Unit tests: named-parameter default values + EXTEND restriction
// (demand 2026-04-28-11).
//
//   - `FUNC f(x: i32, y: i32 = 0)` parses and registers a default in the
//     function signature.
//   - Call site `f(1)` is accepted and the missing argument is filled in
//     by the lowering pass with the default expression.
//   - Call site `f(x: 1)` is accepted (named positional binding).
//   - Call site `f(1, y: 5)` is accepted (positional + named mix).
//   - Required parameter cannot follow a defaulted one (parse-time error).
//   - Default expression must be constant-foldable (literal / unary /
//     binary of literals / pure intra-Ploy call).
//   - Calls that omit a required parameter are rejected.
//   - EXTEND on cpp / rust / java / dotnet / csharp / go is rejected
//     with a sema diagnostic that points at the wrapper-function
//     workflow.
//   - EXTEND on python / ruby / javascript continues to be accepted.
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

struct AnalyzeResult {
  Diagnostics diags;
  std::shared_ptr<polyglot::ploy::Module> module;
  std::unique_ptr<PloySema> sema;
  bool sema_ok{false};
};

AnalyzeResult AnalyzeSource(const std::string &src) {
  AnalyzeResult r;
  PloyLexer lexer(src, "<default_args_test>");
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

bool MessageContains(const Diagnostics &d, const std::string &needle) {
  for (const auto &diag : d.All()) {
    if (diag.message.find(needle) != std::string::npos)
      return true;
  }
  return false;
}

} // namespace

// ----------------------------------------------------------------------------
// 1. Default values are parsed and recorded in the function signature.
// ----------------------------------------------------------------------------

TEST_CASE("FUNC default value is parsed and stored in the signature",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC add(x: i32, y: i32 = 0) -> i32 {
    RETURN x;
}
)");
  REQUIRE(r.sema_ok);
  const auto &sigs = r.sema->KnownSignatures();
  auto it = sigs.find("add");
  REQUIRE(it != sigs.end());
  REQUIRE(it->second.param_count == 2);
  REQUIRE(it->second.param_has_default.size() == 2);
  REQUIRE_FALSE(it->second.param_has_default[0]);
  REQUIRE(it->second.param_has_default[1]);
  REQUIRE(it->second.param_default_values.size() == 2);
  REQUIRE(it->second.param_default_values[1] != nullptr);
}

// ----------------------------------------------------------------------------
// 2. A call that omits a defaulted argument is accepted.
// ----------------------------------------------------------------------------

TEST_CASE("Call site can omit a defaulted trailing argument",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC add(x: i32, y: i32 = 0) -> i32 {
    RETURN x;
}

FUNC use() -> i32 {
    RETURN add(1);
}
)");
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 3. Named-only call site is accepted.
// ----------------------------------------------------------------------------

TEST_CASE("Call site can name the only required argument",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC add(x: i32, y: i32 = 0) -> i32 {
    RETURN x;
}

FUNC use() -> i32 {
    RETURN add(x: 1);
}
)");
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 4. Mixed positional + named call site is accepted.
// ----------------------------------------------------------------------------

TEST_CASE("Call site can mix a positional arg with a trailing named arg",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC add(x: i32, y: i32 = 0) -> i32 {
    RETURN x;
}

FUNC use() -> i32 {
    RETURN add(1, y: 5);
}
)");
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 5. Required parameter after a defaulted one is rejected.
// ----------------------------------------------------------------------------

TEST_CASE("Required parameter after a defaulted one is rejected",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC bad(x: i32, y: i32 = 0, z: i32) -> i32 {
    RETURN x;
}
)");
  // Parser already reports the structural violation.
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "cannot follow a parameter with a default"));
}

// ----------------------------------------------------------------------------
// 6. Default expression must be constant-foldable.
// ----------------------------------------------------------------------------

TEST_CASE("Default expression that reads a parameter is rejected",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC bad(x: i32, y: i32 = x) -> i32 {
    RETURN x;
}
)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "must be a constant expression"));
}

// ----------------------------------------------------------------------------
// 7. Pure-call default is accepted (demand: "CONST 可折叠或纯函数调用").
// ----------------------------------------------------------------------------

TEST_CASE("Default expression may be a pure intra-Ploy call",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC zero() -> i32 {
    RETURN 0;
}

FUNC add(x: i32, y: i32 = zero()) -> i32 {
    RETURN x;
}
)");
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 8. Omitting a required (no-default) parameter is rejected.
// ----------------------------------------------------------------------------

TEST_CASE("Call that omits a required parameter is rejected",
          "[ploy][default_args]") {
  auto r = AnalyzeSource(R"(
FUNC add(x: i32, y: i32 = 0) -> i32 {
    RETURN x;
}

FUNC use() -> i32 {
    RETURN add(y: 5);
}
)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "required parameter 'x'"));
}

// ----------------------------------------------------------------------------
// 9. EXTEND on a static language is rejected.
// ----------------------------------------------------------------------------

TEST_CASE("EXTEND on cpp is rejected by sema",
          "[ploy][extend][restrict]") {
  auto r = AnalyzeSource(R"(
EXTEND(cpp, foo::Bar) AS Wrapper {
    FUNC tick() -> i32 { RETURN 0; }
}
)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "EXTEND is not allowed on statically-typed"));
}

TEST_CASE("EXTEND on java is rejected by sema",
          "[ploy][extend][restrict]") {
  auto r = AnalyzeSource(R"(
EXTEND(java, com::foo::Bar) AS Wrapper {
    FUNC tick() -> i32 { RETURN 0; }
}
)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "EXTEND is not allowed on statically-typed"));
}

TEST_CASE("EXTEND on rust is rejected by sema",
          "[ploy][extend][restrict]") {
  auto r = AnalyzeSource(R"(
EXTEND(rust, tokio::Task) AS Wrapper {
    FUNC tick() -> i32 { RETURN 0; }
}
)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "EXTEND is not allowed on statically-typed"));
}

TEST_CASE("EXTEND on dotnet is rejected by sema",
          "[ploy][extend][restrict]") {
  auto r = AnalyzeSource(R"(
EXTEND(dotnet, System::Object) AS Wrapper {
    FUNC tick() -> i32 { RETURN 0; }
}
)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "EXTEND is not allowed on statically-typed"));
}

// ----------------------------------------------------------------------------
// 10. EXTEND on a dynamic host language is still accepted.
// ----------------------------------------------------------------------------

TEST_CASE("EXTEND on python is accepted",
          "[ploy][extend][restrict]") {
  auto r = AnalyzeSource(R"(
EXTEND(python, torch::nn::Module) AS Net {
    FUNC forward(x: f64) -> f64 { RETURN x; }
}
)");
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("EXTEND on ruby is accepted",
          "[ploy][extend][restrict]") {
  auto r = AnalyzeSource(R"(
EXTEND(ruby, ActiveRecord::Base) AS User {
    FUNC name() -> i32 { RETURN 0; }
}
)");
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("EXTEND on javascript is accepted",
          "[ploy][extend][restrict]") {
  auto r = AnalyzeSource(R"(
EXTEND(javascript, EventEmitter) AS Bus {
    FUNC tick() -> i32 { RETURN 0; }
}
)");
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}
