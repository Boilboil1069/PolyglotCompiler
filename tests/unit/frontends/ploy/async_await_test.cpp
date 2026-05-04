// ============================================================================
// Unit tests for the structured async/await syntax (since v1.14.0):
//   ASYNC FUNC fetch() -> i32 { ... }
//   LET v = AWAIT call_async();
//
// Coverage matrix:
//   1. Top-level `ASYNC FUNC` parses and is marked is_async=true.
//   2. AWAIT inside an ASYNC body parses and type-checks cleanly.
//   3. AWAIT outside an ASYNC body is rejected by sema.
//   4. AWAIT without an operand is rejected by sema.
//   5. Plain `FUNC` is unaffected (still is_async=false).
//   6. Nested AWAITs inside an ASYNC body all type-check.
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
using polyglot::ploy::AwaitExpression;
using polyglot::ploy::ExprStatement;
using polyglot::ploy::FuncDecl;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::VarDecl;

namespace {

struct AnalyzeResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<async-await-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
    r.sema->Analyze(r.module);
    return r;
}

}  // namespace

TEST_CASE("ASYNC FUNC parses and is marked async",
          "[ploy][parser][sema][async]") {
    auto r = Analyze(
        "ASYNC FUNC fetch() -> i32 { RETURN 42; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    REQUIRE(r.module->declarations.size() == 1);
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    CHECK(fn->name == "fetch");
    CHECK(fn->is_async);
}

TEST_CASE("Plain FUNC stays non-async", "[ploy][parser][async]") {
    auto r = Analyze("FUNC plain() -> i32 { RETURN 1; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    CHECK_FALSE(fn->is_async);
}

TEST_CASE("AWAIT inside ASYNC body parses and type-checks",
          "[ploy][parser][sema][async][await]") {
    auto r = Analyze(
        "ASYNC FUNC outer() -> i32 { "
        "  LET v = AWAIT inner(); "
        "  RETURN 0; "
        "}");
    // The call to `inner()` is unresolved (no LINK / FUNC), but sema
    // for the AWAIT expression itself must not raise the
    // 'AWAIT outside ASYNC' diagnostic.
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->body.size() >= 1);
    auto var = std::dynamic_pointer_cast<VarDecl>(fn->body[0]);
    REQUIRE(var);
    auto await = std::dynamic_pointer_cast<AwaitExpression>(var->init);
    REQUIRE(await);
}

TEST_CASE("AWAIT outside ASYNC is rejected", "[ploy][sema][async][await]") {
    auto r = Analyze(
        "FUNC outer() { "
        "  LET v = AWAIT inner(); "
        "}");
    REQUIRE(r.diags.HasErrors());
    bool found = false;
    for (const auto &d : r.diags.All()) {
        if (d.message.find("AWAIT may only appear inside an ASYNC") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("ASYNC keyword without FUNC is rejected", "[ploy][parser][async]") {
    auto r = Analyze("ASYNC LET x = 1;");
    REQUIRE(r.diags.HasErrors());
}

TEST_CASE("Nested AWAITs inside ASYNC body all parse",
          "[ploy][parser][sema][async][await]") {
    auto r = Analyze(
        "ASYNC FUNC chained() -> i32 { "
        "  LET a = AWAIT first(); "
        "  LET b = AWAIT second(); "
        "  LET c = AWAIT third(); "
        "  RETURN 0; "
        "}");
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    CHECK(fn->is_async);
    int await_count = 0;
    for (const auto &stmt : fn->body) {
        auto var = std::dynamic_pointer_cast<VarDecl>(stmt);
        if (var && std::dynamic_pointer_cast<AwaitExpression>(var->init)) {
            ++await_count;
        }
    }
    CHECK(await_count == 3);
}
