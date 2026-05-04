/**
 * @file     visibility_attrs_test.cpp
 * @brief    Unit tests for PUB / PRIVATE visibility and `@name(args)`
 *           attribute prefixes on top-level FUNC and STRUCT declarations
 *           (since v1.16.0).
 *
 * @ingroup  Tests / Ploy / Visibility-Attributes
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::DiagnosticSeverity;
using polyglot::ploy::FuncDecl;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::StructDecl;
using polyglot::ploy::Visibility;

namespace {

struct AnalyzeResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<vis-attrs-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
    r.sema->Analyze(r.module);
    return r;
}

bool DiagsContain(const Diagnostics &diags, const std::string &needle) {
    for (const auto &d : diags.All()) {
        if (d.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("PUB FUNC parses and is marked public", "[ploy][parser][visibility]") {
    auto r = Analyze("PUB FUNC api() -> i32 { RETURN 0; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    CHECK(fn->visibility == Visibility::kPub);
    CHECK(fn->visibility_explicit);
}

TEST_CASE("PRIVATE FUNC parses and is marked private",
          "[ploy][parser][visibility]") {
    auto r = Analyze("PRIVATE FUNC helper() -> i32 { RETURN 0; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    CHECK(fn->visibility == Visibility::kPrivate);
    CHECK(fn->visibility_explicit);
}

TEST_CASE("Default FUNC visibility is private but not explicit",
          "[ploy][parser][visibility]") {
    auto r = Analyze("FUNC plain() -> i32 { RETURN 0; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    CHECK(fn->visibility == Visibility::kPrivate);
    CHECK_FALSE(fn->visibility_explicit);
}

TEST_CASE("PUB STRUCT parses and is marked public",
          "[ploy][parser][visibility][struct]") {
    auto r = Analyze("PUB STRUCT Point { x: i32, y: i32 }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto sd = std::dynamic_pointer_cast<StructDecl>(r.module->declarations[0]);
    REQUIRE(sd);
    CHECK(sd->visibility == Visibility::kPub);
    CHECK(sd->visibility_explicit);
}

TEST_CASE("Built-in attribute parses on FUNC", "[ploy][parser][attributes]") {
    auto r = Analyze("@inline PUB FUNC fast() -> i32 { RETURN 0; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->attributes.size() == 1);
    CHECK(fn->attributes[0].name == "inline");
    CHECK(fn->visibility == Visibility::kPub);
}

TEST_CASE("Multiple attributes parse and order is preserved",
          "[ploy][parser][attributes]") {
    auto r = Analyze(
        "@hot @always_inline PUB FUNC tight() -> i32 { RETURN 0; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->attributes.size() == 2);
    CHECK(fn->attributes[0].name == "hot");
    CHECK(fn->attributes[1].name == "always_inline");
}

TEST_CASE("Attribute with arguments parses and captures arg text",
          "[ploy][parser][attributes]") {
    auto r = Analyze(
        "@deprecated(\"use new_api\") PUB FUNC old() -> i32 { RETURN 0; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    auto fn = std::dynamic_pointer_cast<FuncDecl>(r.module->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->attributes.size() == 1);
    CHECK(fn->attributes[0].name == "deprecated");
    REQUIRE(fn->attributes[0].args.size() == 1);
    CHECK(fn->attributes[0].args[0] == "\"use new_api\"");
}

TEST_CASE("Unknown attribute warns but does not error",
          "[ploy][sema][attributes]") {
    auto r = Analyze(
        "@made_up PUB FUNC api() -> i32 { RETURN 0; }");
    REQUIRE_FALSE(r.diags.HasErrors());
    CHECK(DiagsContain(r.diags, "unknown attribute"));
}

TEST_CASE("EXPORT of explicitly PRIVATE symbol is rejected",
          "[ploy][sema][visibility][export]") {
    auto r = Analyze(R"(
PRIVATE FUNC inner() -> i32 { RETURN 0; }
EXPORT inner;
)");
    REQUIRE(r.diags.HasErrors());
    CHECK(DiagsContain(r.diags, "requires the symbol"));
}

TEST_CASE("EXPORT of PUB symbol succeeds without warning",
          "[ploy][sema][visibility][export]") {
    auto r = Analyze(R"(
PUB FUNC api() -> i32 { RETURN 0; }
EXPORT api AS "api_external";
)");
    REQUIRE_FALSE(r.diags.HasErrors());
    // No deprecation warning should be emitted for explicit PUB.
    for (const auto &d : r.diags.All()) {
        CHECK(d.severity != DiagnosticSeverity::kWarning);
    }
}

TEST_CASE("EXPORT of legacy default-visibility symbol auto-promotes with warning",
          "[ploy][sema][visibility][export]") {
    auto r = Analyze(R"(
FUNC legacy() -> i32 { RETURN 0; }
EXPORT legacy;
)");
    REQUIRE_FALSE(r.diags.HasErrors());
    CHECK(DiagsContain(r.diags, "without an explicit PUB"));
}

TEST_CASE("Visibility / attribute prefix on a non-FUNC is rejected",
          "[ploy][parser][visibility]") {
    auto r = Analyze("PUB CONST K: i32 = 1;");
    CHECK(DiagsContain(r.diags,
                       "PUB / PRIVATE / @attribute prefix is only allowed"));
}

TEST_CASE("Duplicate visibility modifier is reported",
          "[ploy][parser][visibility]") {
    auto r = Analyze("PUB PRIVATE FUNC a() -> i32 { RETURN 0; }");
    CHECK(DiagsContain(r.diags, "duplicate visibility modifier"));
}
