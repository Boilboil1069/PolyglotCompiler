// ============================================================================
// Unit tests: typed cross-language object handles
//   - CLASS schema registration populates ForeignClassSchema
//   - NEW returns HANDLE<lang::T> instead of Any when a schema exists
//   - METHOD type-checks against the registered signature
//   - GET / SET resolve attribute types via the schema
//   - HANDLE<a::T> assignment to HANDLE<b::U> is rejected
//   - Unknown methods on a typed handle emit a warning, not an error
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

// Tiny helper: parse + semantically analyse `src`, return diagnostics.
// Tests use the returned Diagnostics object to assert on errors / warnings.
struct AnalyzeResult {
  Diagnostics diags;
  std::shared_ptr<polyglot::ploy::Module> module;
  std::unique_ptr<PloySema> sema;
  bool sema_ok{false};
};

AnalyzeResult AnalyzeSource(const std::string &src) {
  AnalyzeResult r;
  PloyLexer lexer(src, "<typed_handle_test>");
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
// 1. Schema registration
// ----------------------------------------------------------------------------

TEST_CASE("CLASS schema registers methods and attributes",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::torch::nn::Linear {
    METHOD __init__(in_features: i32, out_features: i32);
    METHOD forward(x: f32) -> f32;
    ATTR in_features: i32;
}
)PLOY");

  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());

  const auto &schemas = r.sema->ClassSchemas();
  auto it = schemas.find("python::torch::nn::Linear");
  REQUIRE(it != schemas.end());
  CHECK(it->second.language == "python");
  CHECK(it->second.class_name == "torch::nn::Linear");
  CHECK(it->second.methods.count("forward") == 1u);
  CHECK(it->second.methods.count("__init__") == 1u);
  CHECK(it->second.attributes.count("in_features") == 1u);
  CHECK(it->second.has_constructor);
}

// ----------------------------------------------------------------------------
// 2. NEW returns a typed HANDLE<lang::T>
// ----------------------------------------------------------------------------

TEST_CASE("NEW with registered CLASS schema produces a typed handle",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::torch::nn::Linear {
    METHOD __init__(in_features: i32, out_features: i32);
    METHOD forward(x: f32) -> f32;
}

FUNC build() -> i32 {
    LET m: HANDLE<python::torch::nn::Linear> = NEW(python, torch::nn::Linear, 10, 5);
    RETURN 0;
}
)PLOY");

  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  INFO(r.diags.FormatAll());
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 3. METHOD argument-type validation against the schema
// ----------------------------------------------------------------------------

TEST_CASE("METHOD with wrong argument count against schema reports error",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::Foo {
    METHOD bar(x: i32) -> i32;
}

FUNC use() -> i32 {
    LET o: HANDLE<python::Foo> = NEW(python, Foo);
    LET y = METHOD(python, o, bar);
    RETURN 0;
}
)PLOY");

  REQUIRE(r.module);
  // Argument-count mismatch must surface as an error.
  REQUIRE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 4. Unknown method on a typed handle is a warning, not an error
// ----------------------------------------------------------------------------

TEST_CASE("Unknown METHOD on typed handle warns, does not error",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::Foo {
    METHOD bar(x: i32) -> i32;
}

FUNC use() -> i32 {
    LET o: HANDLE<python::Foo> = NEW(python, Foo);
    LET z = METHOD(python, o, undeclared_method, 1);
    RETURN 0;
}
)PLOY");

  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
  CHECK(r.diags.HasWarnings());
}

// ----------------------------------------------------------------------------
// 5. Cross-language handle mixing is rejected
// ----------------------------------------------------------------------------

TEST_CASE("HANDLE<a::T> cannot be assigned to HANDLE<b::T>",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::A {
    METHOD __init__();
}
CLASS cpp::A {
    METHOD __init__();
}

FUNC mix() -> i32 {
    LET py: HANDLE<python::A> = NEW(python, A);
    LET cx: HANDLE<cpp::A> = py;
    RETURN 0;
}
)PLOY");

  REQUIRE(r.module);
  // The cross-language assignment must be rejected.
  REQUIRE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 6. GET resolves declared attribute type
// ----------------------------------------------------------------------------

TEST_CASE("GET on typed handle resolves declared ATTR type",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::Box {
    METHOD __init__();
    ATTR width: i32;
}

FUNC read() -> i32 {
    LET b: HANDLE<python::Box> = NEW(python, Box);
    LET w: i32 = GET(python, b, width);
    RETURN w;
}
)PLOY");

  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  INFO(r.diags.FormatAll());
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 7. SET checks value type against declared ATTR type
// ----------------------------------------------------------------------------

TEST_CASE("SET on typed handle rejects mismatched value type",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::Box {
    METHOD __init__();
    ATTR width: i32;
}

FUNC bad_assign() -> i32 {
    LET b: HANDLE<python::Box> = NEW(python, Box);
    SET(python, b, width, "not-an-int");
    RETURN 0;
}
)PLOY");

  REQUIRE(r.module);
  // The value type ('string') is not compatible with the declared ATTR
  // type ('i32'), so sema must report an error.
  REQUIRE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 8. Unknown attribute on typed handle warns, does not error
// ----------------------------------------------------------------------------

TEST_CASE("Unknown ATTR on typed handle GET warns, does not error",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
CLASS python::Box {
    METHOD __init__();
    ATTR width: i32;
}

FUNC dyn() -> i32 {
    LET b: HANDLE<python::Box> = NEW(python, Box);
    LET h = GET(python, b, undeclared);
    RETURN 0;
}
)PLOY");

  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
  CHECK(r.diags.HasWarnings());
}

// ----------------------------------------------------------------------------
// 9. Backward compatibility: NEW without CLASS schema still parses
// ----------------------------------------------------------------------------

TEST_CASE("NEW without CLASS schema still parses (backward compat)",
          "[ploy][sema][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
FUNC legacy() -> i32 {
    LET m = NEW(python, torch::nn::Linear, 10, 5);
    RETURN 0;
}
)PLOY");

  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// 10. Identifier `handle` (lower-case) still works as a variable name
//     — proves HANDLE is a contextual keyword, not a reserved word.
// ----------------------------------------------------------------------------

TEST_CASE("`handle` remains a usable variable identifier",
          "[ploy][lexer][typed_handle]") {
  auto r = AnalyzeSource(R"PLOY(
FUNC use_handle_var() -> i32 {
    LET handle: i32 = 42;
    RETURN handle;
}
)PLOY");

  REQUIRE(r.module);
  REQUIRE(r.sema_ok);
  INFO(r.diags.FormatAll());
  REQUIRE_FALSE(r.diags.HasErrors());
}
