// ============================================================================
// Unit tests for Python frontend per-version syntax gating.
//
// Covers:
//   1. Walrus operator `:=` (PEP 572) — requires Python 3.8 or newer.
//   2. `match` / `case` (PEP 634) — requires Python 3.10 or newer.
//
// Each feature has two checks: ACCEPT under a sufficiently new version, and
// REJECT (with `kLangVersionMismatch`, error code 6001) under an older one.
// The parser keeps producing well-formed AST nodes after reporting the
// mismatch so downstream passes can continue (we report-and-continue rather
// than report-and-skip).
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_versions.h"
#include "frontends/python/include/python_frontend.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_sema.h"
#include "frontends/common/include/sema_context.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::ErrorCode;
using polyglot::frontends::PythonVersion;
using polyglot::python::PythonLexer;
using polyglot::python::PythonParser;

namespace {

// Helper: count how many of the diagnostics carry kLangVersionMismatch.
size_t CountLangVersionMismatches(const Diagnostics &diags) {
  size_t n = 0;
  for (const auto &d : diags.All()) {
    if (d.code == ErrorCode::kLangVersionMismatch)
      ++n;
  }
  return n;
}

// Helper: parse src under the given Python version, returning the
// diagnostics object so the test can inspect it.
Diagnostics ParseWithVersion(const char *src, PythonVersion v) {
  Diagnostics diag;
  PythonLexer lexer(src, "<mem>", &diag);
  PythonParser parser(lexer, diag);
  parser.SetPythonVersion(v);
  parser.ParseModule();
  return diag;
}

} // namespace

// ----------------------------------------------------------------------------
// Walrus operator
// ----------------------------------------------------------------------------

TEST_CASE("Python walrus ':=' is accepted on Python 3.8", "[python][version-gating]") {
  const char *src = "if (n := 10) > 5:\n    pass\n";
  auto diag = ParseWithVersion(src, PythonVersion::kPy3_8);
  REQUIRE(CountLangVersionMismatches(diag) == 0);
}

TEST_CASE("Python walrus ':=' is accepted on Python 3.11",
          "[python][version-gating]") {
  const char *src = "if (n := 10) > 5:\n    pass\n";
  auto diag = ParseWithVersion(src, PythonVersion::kPy3_11);
  REQUIRE(CountLangVersionMismatches(diag) == 0);
}

TEST_CASE("Python walrus ':=' is rejected on Python 3.6", "[python][version-gating]") {
  const char *src = "if (n := 10) > 5:\n    pass\n";
  auto diag = ParseWithVersion(src, PythonVersion::kPy3_6);
  REQUIRE(CountLangVersionMismatches(diag) >= 1);
}

// ----------------------------------------------------------------------------
// match / case
// ----------------------------------------------------------------------------

TEST_CASE("Python 'match' is accepted on Python 3.10", "[python][version-gating]") {
  const char *src =
      "match x:\n"
      "    case 1:\n"
      "        pass\n"
      "    case _:\n"
      "        pass\n";
  auto diag = ParseWithVersion(src, PythonVersion::kPy3_10);
  REQUIRE(CountLangVersionMismatches(diag) == 0);
}

TEST_CASE("Python 'match' is rejected on Python 3.8", "[python][version-gating]") {
  const char *src =
      "match x:\n"
      "    case 1:\n"
      "        pass\n"
      "    case _:\n"
      "        pass\n";
  auto diag = ParseWithVersion(src, PythonVersion::kPy3_8);
  REQUIRE(CountLangVersionMismatches(diag) >= 1);
}

// ----------------------------------------------------------------------------
// kAuto behaves as the configured modern default and accepts every supported feature.
// ----------------------------------------------------------------------------

TEST_CASE("Python kAuto admits walrus and match", "[python][version-gating]") {
  const char *src =
      "if (n := 10) > 5:\n"
      "    pass\n"
      "match n:\n"
      "    case 0:\n"
      "        pass\n";
  auto diag = ParseWithVersion(src, PythonVersion::kAuto);
  REQUIRE(CountLangVersionMismatches(diag) == 0);
}

TEST_CASE("Python PEP 695 type aliases are gated at Python 3.12",
          "[python][version-gating][pep695]") {
  auto old_diag = ParseWithVersion("type Vector[T] = list[T]\n", PythonVersion::kPy3_11);
  REQUIRE(CountLangVersionMismatches(old_diag) == 1);

  auto new_diag = ParseWithVersion("type Vector[T] = list[T]\n", PythonVersion::kPy3_12);
  REQUIRE(CountLangVersionMismatches(new_diag) == 0);
  REQUIRE_FALSE(new_diag.HasErrors());
}

TEST_CASE("Python positional-only parameters are gated at Python 3.8",
          "[python][version-gating][pep570]") {
  for (const char *source : {"def f(value, /):\n    return value\n",
                             "f = lambda value, /: value\n"}) {
    auto old_diag = ParseWithVersion(source, PythonVersion::kPy3_7);
    REQUIRE(CountLangVersionMismatches(old_diag) == 1);

    auto new_diag = ParseWithVersion(source, PythonVersion::kPy3_8);
    REQUIRE(CountLangVersionMismatches(new_diag) == 0);
    REQUIRE_FALSE(new_diag.HasErrors());
  }

  for (const char *source : {"def f(first, /, second, /):\n    pass\n",
                             "f = lambda first, /, second, /: second\n"}) {
    auto invalid_diag = ParseWithVersion(source, PythonVersion::kPy3_14);
    REQUIRE(invalid_diag.HasErrors());
  }
}

TEST_CASE("Python type parameter defaults are gated at Python 3.13",
          "[python][version-gating][pep696]") {
  constexpr const char *source = "type Vector[T = int] = list[T]\n";
  auto old_diag = ParseWithVersion(source, PythonVersion::kPy3_12);
  REQUIRE(CountLangVersionMismatches(old_diag) == 1);

  auto new_diag = ParseWithVersion(source, PythonVersion::kPy3_13);
  REQUIRE(CountLangVersionMismatches(new_diag) == 0);
  REQUIRE_FALSE(new_diag.HasErrors());

  Diagnostics ast_diags;
  PythonLexer lexer("type Bounded[T: int = int] = T\n", "<mem>", &ast_diags);
  PythonParser parser(lexer, ast_diags);
  parser.SetPythonVersion(PythonVersion::kPy3_13);
  parser.ParseModule();
  auto module = parser.TakeModule();
  REQUIRE_FALSE(ast_diags.HasErrors());
  auto alias = std::dynamic_pointer_cast<polyglot::python::TypeAlias>(module->body.at(0));
  REQUIRE(alias);
  REQUIRE(alias->type_parameters.size() == 1);
  CHECK(alias->type_parameters[0].name == "T");
  CHECK(alias->type_parameters[0].bound != nullptr);
  CHECK(alias->type_parameters[0].default_value != nullptr);

  // Defaults and alias values use lazy annotation-scope evaluation, so a
  // forward reference must not be rejected eagerly at declaration time.
  Diagnostics lazy_diags;
  PythonLexer lazy_lexer("type Forward[T = Later] = T\n", "<mem>", &lazy_diags);
  PythonParser lazy_parser(lazy_lexer, lazy_diags);
  lazy_parser.SetPythonVersion(PythonVersion::kPy3_13);
  lazy_parser.ParseModule();
  auto lazy_module = lazy_parser.TakeModule();
  REQUIRE_FALSE(lazy_diags.HasErrors());
  polyglot::frontends::SemaContext sema(lazy_diags);
  polyglot::python::AnalyzeModule(*lazy_module, sema);
  CHECK_FALSE(lazy_diags.HasErrors());

  auto invalid_order = ParseWithVersion(
      "type BadOrder[Default = int, Required] = tuple[Default, Required]\n",
      PythonVersion::kPy3_13);
  CHECK(invalid_order.HasErrors());

  auto ambiguous_after_tuple = ParseWithVersion(
      "type Ambiguous[*Ts, T = int] = tuple[*Ts, T]\n",
      PythonVersion::kPy3_13);
  CHECK(ambiguous_after_tuple.HasErrors());

  Diagnostics variadic_diags;
  PythonLexer variadic_lexer(
      "type Variadic[*Ts = *tuple[int], **P = [int, str]] = tuple\n",
      "<mem>", &variadic_diags);
  PythonParser variadic_parser(variadic_lexer, variadic_diags);
  variadic_parser.SetPythonVersion(PythonVersion::kPy3_13);
  variadic_parser.ParseModule();
  auto variadic_module = variadic_parser.TakeModule();
  REQUIRE_FALSE(variadic_diags.HasErrors());
  auto variadic_alias =
      std::dynamic_pointer_cast<polyglot::python::TypeAlias>(variadic_module->body.at(0));
  REQUIRE(variadic_alias);
  REQUIRE(variadic_alias->type_parameters.size() == 2);
  CHECK(std::dynamic_pointer_cast<polyglot::python::StarredExpression>(
            variadic_alias->type_parameters[0].default_value) != nullptr);
}

TEST_CASE("Python except-star is gated at Python 3.11",
          "[python][version-gating][exception-group]") {
  const char *source = "try:\n    pass\nexcept* ValueError:\n    pass\n";
  auto old_diag = ParseWithVersion(source, PythonVersion::kPy3_10);
  REQUIRE(CountLangVersionMismatches(old_diag) == 1);
  auto new_diag = ParseWithVersion(source, PythonVersion::kPy3_11);
  REQUIRE(CountLangVersionMismatches(new_diag) == 0);
  REQUIRE_FALSE(new_diag.HasErrors());
}

TEST_CASE("Python template strings are gated at Python 3.14",
          "[python][version-gating][tstring]") {
  auto old_diag = ParseWithVersion("template = t\"value={value}\"\n", PythonVersion::kPy3_13);
  REQUIRE(CountLangVersionMismatches(old_diag) == 1);
  auto new_diag = ParseWithVersion("template = t\"value={value}\"\n", PythonVersion::kPy3_14);
  REQUIRE(CountLangVersionMismatches(new_diag) == 0);
  REQUIRE_FALSE(new_diag.HasErrors());
}

TEST_CASE("Python PEP 758 unparenthesized exception lists are gated at Python 3.14",
          "[python][version-gating][pep758]") {
  const char *source =
      "try:\n"
      "    pass\n"
      "except ValueError, TypeError as error:\n"
      "    pass\n";
  auto old_diag = ParseWithVersion(source, PythonVersion::kPy3_13);
  REQUIRE(CountLangVersionMismatches(old_diag) == 1);

  auto new_diag = ParseWithVersion(source, PythonVersion::kPy3_14);
  REQUIRE(CountLangVersionMismatches(new_diag) == 0);
  REQUIRE_FALSE(new_diag.HasErrors());
}

TEST_CASE("Python signature extraction preserves the requested non-default version",
          "[python][version-gating][signatures]") {
  const std::string source =
      "def identity[T](value: T) -> T:\n"
      "    return value\n";
  polyglot::python::PythonLanguageFrontend frontend;

  polyglot::frontends::FrontendOptions modern_options;
  modern_options.python_version = PythonVersion::kPy3_12;
  Diagnostics modern_diagnostics;
  auto signatures = frontend.ExtractSignatures(source, "<mem>", "generic",
                                                modern_diagnostics, modern_options);
  REQUIRE_FALSE(modern_diagnostics.HasErrors());
  REQUIRE(signatures.size() == 1);
  REQUIRE(signatures[0].name == "identity");

  polyglot::frontends::FrontendOptions old_options;
  old_options.python_version = PythonVersion::kPy3_11;
  Diagnostics old_diagnostics;
  REQUIRE(frontend
              .ExtractSignatures(source, "<mem>", "generic", old_diagnostics, old_options)
              .empty());
  REQUIRE(CountLangVersionMismatches(old_diagnostics) == 1);
}
