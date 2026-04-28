// ============================================================================
// Unit tests for the `PRINTLN "literal";` statement (demand 2026-04-28-49 B2).
//
// PRINTLN is the very first runtime-IO primitive in .ploy. It accepts a single
// string literal and (eventually, once B3+B4 land) lowers to a WriteFile call
// against the host's standard-output handle. For Stage B2 we only validate
// the front-end shape: lexer recognition, parser AST, sema acceptance, and
// the relevant error-recovery paths.
//
// Coverage matrix:
//   1. Top-level PRINTLN is parsed into a `PrintlnStmt` whose `message` is the
//      *unquoted* lexeme, with backslash escapes preserved literally.
//   2. The keyword is case-insensitive (`println`, `Println`, `PRINTLN` all work).
//   3. PRINTLN inside a function body is parsed via the same `ParseStatement`
//      dispatch and yields the same node shape.
//   4. A missing string literal (e.g. `PRINTLN 42;`) reports a diagnostic and
//      the parser resyncs to the next statement instead of crashing.
//   5. Sema accepts a well-formed PRINTLN without producing any errors,
//      including the empty-message corner case `PRINTLN "";`.
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
using polyglot::ploy::FuncDecl;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::PrintlnStmt;
using polyglot::ploy::Statement;

namespace {

struct ParseResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
};

// Small helper that mirrors the convention of the other ploy unit tests:
// run the lexer + parser on a source snippet and hand back both the module
// and its diagnostics so callers can check warnings/errors after the fact.
ParseResult ParseSource(const std::string &code) {
    ParseResult r;
    PloyLexer lexer(code, "<println-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    return r;
}

}  // namespace

TEST_CASE("PRINTLN top-level produces a PrintlnStmt with the unquoted message",
          "[ploy][parser][println]") {
    auto result = ParseSource("PRINTLN \"Hello, world!\";\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.module);
    REQUIRE(result.module->declarations.size() == 1);

    auto stmt = std::dynamic_pointer_cast<PrintlnStmt>(
        result.module->declarations[0]);
    REQUIRE(stmt);
    CHECK(stmt->message == "Hello, world!");
}

TEST_CASE("PRINTLN preserves backslash escape bytes verbatim for downstream codegen",
          "[ploy][parser][println]") {
    // The lexer does NOT decode escapes; it is the eventual codegen stage's
    // job to interpret \r\n etc. We assert that contract here so a future
    // refactor that adds early decoding will trip this test before silently
    // changing the IR shape.
    auto result = ParseSource("PRINTLN \"line\\r\\n\";\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.module->declarations.size() == 1);

    auto stmt = std::dynamic_pointer_cast<PrintlnStmt>(
        result.module->declarations[0]);
    REQUIRE(stmt);
    CHECK(stmt->message == "line\\r\\n");
}

TEST_CASE("PRINTLN keyword is case-insensitive like every other ploy keyword",
          "[ploy][lexer][println]") {
    for (const std::string &spelling : {"PRINTLN", "println", "PrintLn", "PRINTln"}) {
        CAPTURE(spelling);
        auto result = ParseSource(spelling + " \"ok\";\n");
        REQUIRE_FALSE(result.diags.HasErrors());
        REQUIRE(result.module->declarations.size() == 1);
        auto stmt = std::dynamic_pointer_cast<PrintlnStmt>(
            result.module->declarations[0]);
        REQUIRE(stmt);
        CHECK(stmt->message == "ok");
    }
}

TEST_CASE("PRINTLN inside a function body parses through ParseStatement dispatch",
          "[ploy][parser][println]") {
    const std::string src =
        "FUNC main() {\n"
        "  PRINTLN \"inside\";\n"
        "}\n";
    auto result = ParseSource(src);
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.module->declarations.size() == 1);

    auto fn = std::dynamic_pointer_cast<FuncDecl>(result.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->body.size() == 1);

    auto stmt = std::dynamic_pointer_cast<PrintlnStmt>(fn->body[0]);
    REQUIRE(stmt);
    CHECK(stmt->message == "inside");
}

TEST_CASE("PRINTLN without a string literal reports a diagnostic and recovers",
          "[ploy][parser][println][error]") {
    // After PRINTLN we hand the parser a numeric literal; this must surface
    // an error rather than silently producing junk, and recovery must let
    // the following well-formed statement still parse.
    const std::string src =
        "PRINTLN 42;\n"
        "PRINTLN \"after\";\n";
    auto result = ParseSource(src);
    CHECK(result.diags.HasErrors());
    REQUIRE(result.module);
    // The second statement must still come through after Sync() recovery.
    bool saw_after = false;
    for (const auto &decl : result.module->declarations) {
        if (auto p = std::dynamic_pointer_cast<PrintlnStmt>(decl)) {
            if (p->message == "after") {
                saw_after = true;
            }
        }
    }
    CHECK(saw_after);
}

TEST_CASE("Sema accepts PRINTLN including the empty-message corner case",
          "[ploy][sema][println]") {
    auto result = ParseSource(
        "PRINTLN \"hello\";\n"
        "PRINTLN \"\";\n");
    REQUIRE_FALSE(result.diags.HasErrors());

    PloySemaOptions opts;
    opts.enable_package_discovery = false;
    PloySema sema(result.diags, opts);
    const bool ok = sema.Analyze(result.module);
    CHECK(ok);
    CHECK_FALSE(result.diags.HasErrors());
}
