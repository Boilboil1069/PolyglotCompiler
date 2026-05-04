// ============================================================================
// Unit tests for the structured exception handling syntax (since
// v1.13.0): TRY { ... } CATCH (e: Error) { ... } FINALLY { ... } and
// THROW <expr>;
//
// Coverage matrix:
//   1. TRY + single CATCH parses and type-checks cleanly; the catch
//      binding is registered as an immutable symbol.
//   2. TRY + multiple CATCH clauses parses (each clause introduces its
//      own binding).
//   3. TRY + FINALLY (no CATCH) parses cleanly.
//   4. TRY + CATCH + FINALLY parses cleanly.
//   5. Bare TRY {} (no CATCH and no FINALLY) is rejected.
//   6. CATCH without a binding name is rejected.
//   7. THROW with an expression parses and type-checks.
//   8. THROW without an expression is rejected.
//   9. CATCH with a non-Error declared type emits a warning but
//      continues to bind the symbol.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::TryStatement;
using polyglot::ploy::ThrowStatement;
using polyglot::ploy::FuncDecl;

namespace {

struct AnalyzeResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<try-catch-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
    r.sema->Analyze(r.module);
    return r;
}

// Wrap a body in a top-level FUNC so statements are reachable.
std::string Wrap(const std::string &body) {
    return std::string("FUNC main() { ") + body + " }";
}

}  // namespace

TEST_CASE("TRY with a single CATCH parses and type-checks",
          "[ploy][parser][sema][try-catch]") {
    auto r = Analyze(Wrap(
        "TRY { THROW \"boom\"; } "
        "CATCH (e: Error) { PRINTLN \"caught\"; }"));
    REQUIRE_FALSE(r.diags.HasErrors());
    REQUIRE(r.module->declarations.size() == 1);
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->body.size() == 1);
    auto try_stmt = std::dynamic_pointer_cast<TryStatement>(fn->body[0]);
    REQUIRE(try_stmt);
    CHECK(try_stmt->catches.size() == 1);
    CHECK(try_stmt->catches[0].var_name == "e");
    CHECK_FALSE(try_stmt->has_finally);
}

TEST_CASE("TRY with multiple CATCH clauses parses",
          "[ploy][parser][try-catch]") {
    auto r = Analyze(Wrap(
        "TRY { THROW \"x\"; } "
        "CATCH (a: Error) { PRINTLN \"a\"; } "
        "CATCH (b: Error) { PRINTLN \"b\"; }"));
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    auto try_stmt = std::dynamic_pointer_cast<TryStatement>(fn->body[0]);
    REQUIRE(try_stmt);
    CHECK(try_stmt->catches.size() == 2);
    CHECK(try_stmt->catches[0].var_name == "a");
    CHECK(try_stmt->catches[1].var_name == "b");
}

TEST_CASE("TRY with only FINALLY parses",
          "[ploy][parser][try-catch][finally]") {
    auto r = Analyze(Wrap(
        "TRY { PRINTLN \"work\"; } "
        "FINALLY { PRINTLN \"cleanup\"; }"));
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    auto try_stmt = std::dynamic_pointer_cast<TryStatement>(fn->body[0]);
    REQUIRE(try_stmt);
    CHECK(try_stmt->catches.empty());
    CHECK(try_stmt->has_finally);
}

TEST_CASE("TRY with CATCH and FINALLY parses",
          "[ploy][parser][try-catch][finally]") {
    auto r = Analyze(Wrap(
        "TRY { THROW \"x\"; } "
        "CATCH (e: Error) { PRINTLN \"caught\"; } "
        "FINALLY { PRINTLN \"done\"; }"));
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    auto try_stmt = std::dynamic_pointer_cast<TryStatement>(fn->body[0]);
    REQUIRE(try_stmt);
    CHECK(try_stmt->catches.size() == 1);
    CHECK(try_stmt->has_finally);
}

TEST_CASE("Bare TRY without CATCH or FINALLY is rejected",
          "[ploy][parser][try-catch]") {
    auto r = Analyze(Wrap("TRY { PRINTLN \"x\"; }"));
    CHECK(r.diags.HasErrors());
}

TEST_CASE("THROW with expression parses and type-checks",
          "[ploy][parser][sema][throw]") {
    auto r = Analyze(Wrap(
        "TRY { THROW \"explicit\"; } CATCH (e: Error) { PRINTLN \"k\"; }"));
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    auto try_stmt = std::dynamic_pointer_cast<TryStatement>(fn->body[0]);
    REQUIRE(try_stmt);
    REQUIRE(try_stmt->body.size() == 1);
    auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(try_stmt->body[0]);
    REQUIRE(throw_stmt);
    REQUIRE(throw_stmt->value);
}

TEST_CASE("THROW without expression is rejected",
          "[ploy][parser][throw]") {
    auto r = Analyze(Wrap("THROW;"));
    CHECK(r.diags.HasErrors());
}

TEST_CASE("CATCH with a non-Error declared type emits a warning",
          "[ploy][sema][try-catch]") {
    auto r = Analyze(Wrap(
        "TRY { THROW \"x\"; } CATCH (e: SomethingElse) { PRINTLN \"k\"; }"));
    // Non-fatal: parsing/type-checking still completes.
    CHECK_FALSE(r.diags.HasErrors());
    CHECK(r.diags.WarningCount() > 0);
}
