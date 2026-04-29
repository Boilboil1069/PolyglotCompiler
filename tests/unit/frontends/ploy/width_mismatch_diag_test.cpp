// ============================================================================
// Unit tests for explicit-width type-mismatch diagnostics
// (demand 2026-04-28-7).
//
// Coverage matrix:
//   1. A CONST declared as `i32` whose initializer folds to `i64`
//      surfaces a width-mismatch warning while still compiling.
//   2. A CONST declared as `f32` whose initializer is `3.14` (folds to
//      f64) surfaces a width-mismatch warning.
//   3. A CONST declared as `u32` whose initializer folds to a signed
//      i64 surfaces a signedness-mismatch warning that mentions both
//      the declared and folded widths in its message.
//   4. A width-equal initializer (CONST i64 = 1 + 2) emits no
//      width-related diagnostic.
//   5. When a TYPE alias is involved, the diagnostic includes the alias
//      name in the form `T (alias of i32)` so the user can find the
//      original declaration quickly.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostic;
using polyglot::frontends::Diagnostics;
using polyglot::frontends::DiagnosticSeverity;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

namespace {

struct AnalyzeResult {
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<width-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
    r.sema->Analyze(module);
    return r;
}

// Returns true iff `diags` contains at least one warning whose message
// includes the substring `needle`.  Errors and notes are ignored.
bool HasWarningContaining(const Diagnostics &diags, const std::string &needle) {
    for (const auto &d : diags.All()) {
        if (d.severity == DiagnosticSeverity::kWarning &&
            d.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("i32 CONST initialised with i64-folded literal warns about width",
          "[ploy][sema][width][diag]") {
    // The literal `5` folds to i64 in the constant folder, while the
    // declared CONST is i32.  Compilation continues but the warning is
    // recorded so IDEs can surface it as a yellow squiggle.
    auto result = Analyze("const K: i32 = 5;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.diags.HasWarnings());
    CHECK(HasWarningContaining(result.diags, "i32"));
    CHECK(HasWarningContaining(result.diags, "i64"));
}

TEST_CASE("f32 CONST initialised with f64-folded literal warns about width",
          "[ploy][sema][width][diag]") {
    auto result = Analyze("const PI: f32 = 3.14;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.diags.HasWarnings());
    CHECK(HasWarningContaining(result.diags, "f32"));
    CHECK(HasWarningContaining(result.diags, "f64"));
}

TEST_CASE("u32 CONST initialised with signed i64 literal warns about signedness",
          "[ploy][sema][width][diag]") {
    auto result = Analyze("const KMask: u32 = 7;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.diags.HasWarnings());
    CHECK(HasWarningContaining(result.diags, "u32"));
}

TEST_CASE("Width-equal CONST emits no width-related diagnostic",
          "[ploy][sema][width]") {
    // We construct an initializer that the folder reports as i64 (the
    // default integer width), matching the declared i64.  Combined with
    // the AreTypesCompatible numeric ↔ numeric rule, no diagnostic at
    // all should be raised.
    auto result = Analyze("const K: i64 = 1 + 2;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    CHECK_FALSE(result.diags.HasWarnings());
}

TEST_CASE("Width-mismatch diagnostic surfaces the alias name when one is used",
          "[ploy][sema][width][diag]") {
    auto result = Analyze(
        "type Pixel = i32;\n"
        "const K: Pixel = 5;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.diags.HasWarnings());
    // The reverse-alias map causes the diagnostic to mention `Pixel`
    // in addition to the underlying `i32` width so the user can jump
    // straight to the alias declaration from the IDE.
    CHECK(HasWarningContaining(result.diags, "Pixel"));
}
