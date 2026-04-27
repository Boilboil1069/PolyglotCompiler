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
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"

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

TEST_CASE("Python walrus ':=' is accepted on Python 3.11 (default)",
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
// kAuto behaves as the default (3.11) and accepts every supported feature.
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
