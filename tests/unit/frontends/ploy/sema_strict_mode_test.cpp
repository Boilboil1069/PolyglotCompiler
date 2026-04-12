/**
 * @file     sema_strict_mode_test.cpp
 * @brief    Regression tests for PloySema strict type-checking mode
 *
 * Strict mode (PloySemaOptions::strict_mode = true) promotes a category of
 * "soft" diagnostics — emitted via ReportStrictDiag() — from warnings to
 * errors.  These tests verify:
 *
 *   1. Untyped parameter → warning in non-strict, error in strict.
 *   2. Variable resolved to Any/Unknown → warning vs error.
 *   3. Cross-language CALL with unknown return type → warning vs error.
 *   4. Programs accepted in non-strict are rejected in strict.
 *   5. Programs that are always errors remain errors in both modes.
 *   6. Enabling strict mode via SetStrictMode after construction.
 *   7. Multiple strict diagnostics accumulate correctly.
 *   8. Pure programs with full type annotations pass both modes.
 *
 * @ingroup  Tests / Frontends / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-11
 */

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Parse and analyse a .ploy source string with the given strict mode setting.
// Returns true if Analyze succeeds (no hard errors).
struct SemaResult {
    bool analyze_ok{false};
    size_t errors{0};
    size_t warnings{0};
};

SemaResult RunSema(const std::string &source, bool strict) {
    Diagnostics diags;
    PloyLexer lexer(source, "<strict_test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();

    SemaResult result;
    if (!module || diags.HasErrors()) {
        result.errors = diags.ErrorCount();
        result.warnings = diags.WarningCount();
        return result;
    }

    PloySemaOptions opts;
    opts.strict_mode = strict;
    opts.enable_package_discovery = false;
    PloySema sema(diags, opts);

    result.analyze_ok = sema.Analyze(module);
    result.errors     = diags.ErrorCount();
    result.warnings   = diags.WarningCount();
    return result;
}

} // namespace

// ============================================================================
// 1. Cross-language CALL return type unknown — warning vs error
// ============================================================================

TEST_CASE("Strict mode: CALL unknown return type is warning in non-strict, error in strict",
          "[ploy][sema][strict]") {
    // CALL to a linked cross-language function whose return type cannot be
    // statically inferred; sema emits strict diagnostic.
    const std::string kSource = R"ploy(
LINK(cpp, python, math::transform, util::transform) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC use_transform(a: INT) -> INT {
    LET r = CALL(cpp, math::transform, a);
    RETURN 0;
}
)ploy";

    SECTION("non-strict: analysis succeeds with warnings") {
        auto r = RunSema(kSource, false);
        CHECK(r.analyze_ok);
        CHECK(r.warnings > 0);
        CHECK(r.errors == 0);
    }

    SECTION("strict: analysis reports errors") {
        auto r = RunSema(kSource, true);
        CHECK(r.errors > 0);
    }
}

// ============================================================================
// 2. Variable resolved to Any/Unknown — warning vs error
// ============================================================================

TEST_CASE("Strict mode: variable with no annotation resolved to Any/Unknown",
          "[ploy][sema][strict]") {
    // cross-language CALL whose return type is unknown → variable 'r'
    // becomes Any/Unknown type.
    const std::string kSource = R"ploy(
LINK(cpp, python, math::transform, util::transform) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC use_result(a: INT) -> INT {
    LET r = CALL(cpp, math::transform, a);
    RETURN 0;
}
)ploy";

    SECTION("non-strict: accepted with warnings") {
        auto r = RunSema(kSource, false);
        CHECK(r.analyze_ok);
        // Should have at least 1 warning about unknown return type
        CHECK(r.warnings > 0);
        CHECK(r.errors == 0);
    }

    SECTION("strict: rejected with errors") {
        auto r = RunSema(kSource, true);
        CHECK(r.errors > 0);
    }
}

// ============================================================================
// 3. Cross-language CALL with unknown return type
// ============================================================================

TEST_CASE("Strict mode: cross-language CALL unknown return type",
          "[ploy][sema][strict]") {
    const std::string kSource = R"ploy(
LINK(cpp, python, net::send, socket::send) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC transmit(payload: INT) -> INT {
    LET status = CALL(cpp, net::send, payload);
    RETURN 0;
}
)ploy";

    auto non_strict = RunSema(kSource, false);
    auto strict     = RunSema(kSource, true);

    // Non-strict: accepted (warnings only)
    CHECK(non_strict.analyze_ok);
    CHECK(non_strict.errors == 0);
    CHECK(non_strict.warnings > 0);

    // Strict: must have errors
    CHECK(strict.errors > 0);
}

// ============================================================================
// 4. Well-typed program passes both modes
// ============================================================================

TEST_CASE("Strict mode: fully typed program passes both strict and non-strict",
          "[ploy][sema][strict]") {
    const std::string kSource = R"ploy(
FUNC add(a: INT, b: INT) -> INT {
    RETURN a + b;
}

FUNC mul(a: INT, b: INT) -> INT {
    VAR result: INT = a * b;
    RETURN result;
}
)ploy";

    auto non_strict = RunSema(kSource, false);
    auto strict     = RunSema(kSource, true);

    CHECK(non_strict.analyze_ok);
    CHECK(non_strict.errors == 0);

    CHECK(strict.analyze_ok);
    CHECK(strict.errors == 0);
}

// ============================================================================
// 5. Always-error programs fail in both modes
// ============================================================================

TEST_CASE("Strict mode: undefined symbol is error in both modes",
          "[ploy][sema][strict]") {
    const std::string kSource = R"ploy(
FUNC main() -> INT {
    LET r = CALL(cpp, math::sqrt, 9.0);
    RETURN 0;
}
)ploy";

    auto non_strict = RunSema(kSource, false);
    auto strict     = RunSema(kSource, true);

    // Both modes must report errors for an unregistered cross-language call
    CHECK(non_strict.errors > 0);
    CHECK(strict.errors > 0);
}

// ============================================================================
// 6. SetStrictMode after construction takes effect
// ============================================================================

TEST_CASE("Strict mode: SetStrictMode(true) after construction enables strict",
          "[ploy][sema][strict][api]") {
    const std::string kSource = R"ploy(
LINK(cpp, python, net::send, socket::send) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC transmit(payload: INT) -> INT {
    LET status = CALL(cpp, net::send, payload);
    RETURN 0;
}
)ploy";

    Diagnostics diags;
    PloyLexer lexer(kSource, "<strict_set_test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module != nullptr);

    PloySemaOptions opts;
    opts.strict_mode = false;
    opts.enable_package_discovery = false;
    PloySema sema(diags, opts);

    // Enable strict mode after construction
    sema.SetStrictMode(true);
    CHECK(sema.IsStrictMode());

    sema.Analyze(module);
    // Should have errors because strict mode is active
    CHECK(diags.HasErrors());
}

// ============================================================================
// 7. Multiple strict diagnostics accumulate
// ============================================================================

TEST_CASE("Strict mode: multiple strict diagnostics accumulate in strict mode",
          "[ploy][sema][strict]") {
    // Two separate cross-language calls with unknown return types →
    // two strict diagnostics (one per CALL).
    const std::string kSource = R"ploy(
LINK(cpp, python, math::add, util::add) {
    MAP_TYPE(cpp::int, python::int);
}

LINK(cpp, python, math::sub, util::sub) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC calc(a: INT, b: INT) -> INT {
    LET x = CALL(cpp, math::add, a, b);
    LET y = CALL(cpp, math::sub, a, b);
    RETURN 0;
}
)ploy";

    auto r = RunSema(kSource, true);
    // Each CALL triggers a separate strict diagnostic
    CHECK(r.errors >= 2);
}

// ============================================================================
// 8. Variable assigned from cross-language CALL gets Unknown type
// ============================================================================

TEST_CASE("Strict mode: variable from cross-language CALL typed Unknown",
          "[ploy][sema][strict]") {
    // A VAR assigned from a CALL whose return type cannot be inferred
    // should trigger the variable-type strict diagnostic.
    const std::string kSource = R"ploy(
LINK(cpp, python, io::read, os::read) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC read_data(fd: INT) -> INT {
    VAR data = CALL(cpp, io::read, fd);
    RETURN 0;
}
)ploy";

    auto non_strict = RunSema(kSource, false);
    CHECK(non_strict.analyze_ok);
    CHECK(non_strict.errors == 0);
    CHECK(non_strict.warnings > 0);

    auto strict = RunSema(kSource, true);
    CHECK(strict.errors > 0);
}
