// ============================================================================
// Unit tests for generic FUNC / STRUCT declarations (since v1.15.0):
//   FUNC max<T: Comparable>(a: T, b: T) -> T { ... }
//   STRUCT Pair<A, B> { first: A, second: B }
//   FUNC f<T>(x: T) WHERE T: Numeric { ... }
//
// Coverage matrix:
//   1. Generic FUNC parses and records type parameters with bounds.
//   2. Multiple type parameters parse in order.
//   3. WHERE clause merges into the matching parameter's bounds.
//   4. WHERE clause referencing an unknown parameter is rejected.
//   5. Unknown built-in bound is rejected by sema.
//   6. Generic STRUCT parses and records type parameters.
//   7. STRUCT instantiation `Pair<i32, String>` resolves cleanly.
//   8. Plain FUNC stays non-generic (type_params is empty).
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
using polyglot::ploy::StructDecl;

namespace {

struct AnalyzeResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<generics-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
    r.sema->Analyze(r.module);
    return r;
}

}  // namespace

TEST_CASE("Generic FUNC parses with bounded type parameter",
          "[ploy][parser][sema][generics]") {
    auto r = Analyze(
        "FUNC max<T: Comparable>(a: T, b: T) -> T { RETURN a; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    REQUIRE(r.module->declarations.size() == 1);
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->type_params.size() == 1);
    CHECK(fn->type_params[0].name == "T");
    REQUIRE(fn->type_params[0].bounds.size() == 1);
    CHECK(fn->type_params[0].bounds[0] == "Comparable");
}

TEST_CASE("Generic FUNC parses with multiple type parameters",
          "[ploy][parser][generics]") {
    auto r = Analyze(
        "FUNC zip<A, B>(a: A, b: B) -> A { RETURN a; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->type_params.size() == 2);
    CHECK(fn->type_params[0].name == "A");
    CHECK(fn->type_params[1].name == "B");
    CHECK(fn->type_params[0].bounds.empty());
    CHECK(fn->type_params[1].bounds.empty());
}

TEST_CASE("WHERE clause merges into type parameter bounds",
          "[ploy][parser][sema][generics][where]") {
    auto r = Analyze(
        "FUNC f<T>(x: T) -> T WHERE T: Numeric { RETURN x; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->type_params.size() == 1);
    REQUIRE(fn->type_params[0].bounds.size() == 1);
    CHECK(fn->type_params[0].bounds[0] == "Numeric");
}

TEST_CASE("WHERE clause referencing unknown parameter is rejected",
          "[ploy][parser][generics][where]") {
    auto r = Analyze(
        "FUNC f<T>(x: T) -> T WHERE U: Numeric { RETURN x; }");
    CHECK(r.diags.HasErrors());
    bool found = false;
    for (const auto &d : r.diags.All()) {
        if (d.message.find("unknown type parameter") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Unknown built-in bound is rejected by sema",
          "[ploy][sema][generics][bounds]") {
    auto r = Analyze(
        "FUNC f<T: Frobnicable>(x: T) -> T { RETURN x; }");
    CHECK(r.diags.HasErrors());
    bool found = false;
    for (const auto &d : r.diags.All()) {
        if (d.message.find("unknown trait bound") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Generic STRUCT parses and records type parameters",
          "[ploy][parser][sema][generics][struct]") {
    auto r = Analyze(
        "STRUCT Pair<A, B> { first: A, second: B }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto sd = std::dynamic_pointer_cast<StructDecl>(r.module->declarations[0]);
    REQUIRE(sd);
    REQUIRE(sd->type_params.size() == 2);
    CHECK(sd->type_params[0].name == "A");
    CHECK(sd->type_params[1].name == "B");
    REQUIRE(sd->fields.size() == 2);
    CHECK(sd->fields[0].name == "first");
    CHECK(sd->fields[1].name == "second");
}

TEST_CASE("Generic STRUCT instantiation in field type resolves cleanly",
          "[ploy][sema][generics][struct]") {
    auto r = Analyze(
        "STRUCT Pair<A, B> { first: A, second: B } "
        "STRUCT Holder { p: Pair<i32, STRING> }");
    CHECK_FALSE(r.diags.HasErrors());
}

TEST_CASE("Plain FUNC stays non-generic",
          "[ploy][parser][generics]") {
    auto r = Analyze("FUNC plain() -> i32 { RETURN 1; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    CHECK(fn->type_params.empty());
}
