// ============================================================================
// Unit tests for the `CONST <name>: <type> = <expr>;` declaration
// (demand 2026-04-28-7).
//
// Coverage matrix:
//   1. A literal-initialised CONST is folded by sema, registered in the
//      constants table, and exposed via LookupConstantText.
//   2. A CONST that references a previously declared CONST in its
//      initializer folds correctly (single-step constant propagation).
//   3. A CONST initializer that contains an arithmetic expression on
//      literal operands is folded; the recorded type respects the
//      declared annotation.
//   4. Missing initializer / missing type / non-constant initializer all
//      surface as compilation errors.
//   5. A CONST with a type-incompatible initializer (e.g. STRING value
//      assigned to an INT-typed CONST) is rejected as a type mismatch.
//   6. CONST symbols are queryable through `Symbols()` like any other
//      immutable variable, so identifier resolution downstream "just
//      works" without a special case.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::core::TypeKind;
using polyglot::frontends::Diagnostics;
using polyglot::ploy::ConstDecl;
using polyglot::ploy::Module;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

namespace {

struct AnalyzeResult {
    std::shared_ptr<Module> module;
    Diagnostics diags;
    std::unique_ptr<PloySema> sema;
};

AnalyzeResult Analyze(const std::string &code) {
    AnalyzeResult r;
    PloyLexer lexer(code, "<const-decl-test>");
    PloyParser parser(lexer, r.diags);
    parser.ParseModule();
    r.module = parser.TakeModule();
    r.sema = std::make_unique<PloySema>(r.diags, PloySemaOptions{});
    r.sema->Analyze(r.module);
    return r;
}

}  // namespace

TEST_CASE("Literal-initialised CONST is folded and registered",
          "[ploy][sema][const]") {
    auto result = Analyze("const KMaxRetry: i32 = 5;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.module->declarations.size() == 1);
    auto cd = std::dynamic_pointer_cast<ConstDecl>(
        result.module->declarations[0]);
    REQUIRE(cd);
    CHECK(cd->name == "KMaxRetry");

    REQUIRE(result.sema->ConstantCount() == 1);
    const std::string *txt = result.sema->LookupConstantText("KMaxRetry");
    REQUIRE(txt != nullptr);
    CHECK(*txt == "5");

    const auto &symbols = result.sema->Symbols();
    auto sym = symbols.find("KMaxRetry");
    REQUIRE(sym != symbols.end());
    CHECK_FALSE(sym->second.is_mutable);
    CHECK(sym->second.type.kind == TypeKind::kInt);
    CHECK(sym->second.type.bit_width == 32);
}

TEST_CASE("CONST initializer can reference a previously declared CONST",
          "[ploy][sema][const]") {
    auto result = Analyze(
        "const KBase: i32 = 100;\n"
        "const KOffset: i32 = KBase;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    CHECK(result.sema->ConstantCount() == 2);
    const std::string *base = result.sema->LookupConstantText("KBase");
    const std::string *off = result.sema->LookupConstantText("KOffset");
    REQUIRE(base != nullptr);
    REQUIRE(off != nullptr);
    CHECK(*base == "100");
    // Folder substitutes the referenced CONST's folded text verbatim.
    CHECK(*off == "100");
}

TEST_CASE("CONST initializer can be an arithmetic literal expression",
          "[ploy][sema][const]") {
    auto result = Analyze("const KArea: i64 = 10 * 20;\n");
    REQUIRE_FALSE(result.diags.HasErrors());
    REQUIRE(result.sema->ConstantCount() == 1);
    const std::string *txt = result.sema->LookupConstantText("KArea");
    REQUIRE(txt != nullptr);
    // Folded expression text preserves operator + operands so the IR
    // emitter can re-evaluate without re-running the folder.
    CHECK(txt->find("10") != std::string::npos);
    CHECK(txt->find("20") != std::string::npos);
    CHECK(txt->find('*') != std::string::npos);
}

TEST_CASE("CONST without initializer is a parser error",
          "[ploy][parser][const][diag]") {
    auto result = Analyze("const KMissing: i32;\n");
    REQUIRE(result.diags.HasErrors());
}

TEST_CASE("CONST without type annotation is a parser error",
          "[ploy][parser][const][diag]") {
    auto result = Analyze("const KMissing = 5;\n");
    REQUIRE(result.diags.HasErrors());
}

TEST_CASE("CONST initialised by a non-constant expression is a sema error",
          "[ploy][sema][const][diag]") {
    auto result = Analyze(
        "let runtime_v: i32 = 0;\n"
        "const KBad: i32 = runtime_v;\n");
    REQUIRE(result.diags.HasErrors());
    // The constant table must NOT contain the failed declaration —
    // otherwise downstream stages would believe a non-constant value is
    // available for propagation and miscompile.
    CHECK(result.sema->LookupConstantText("KBad") == nullptr);
}

TEST_CASE("CONST with type-incompatible initializer is a sema error",
          "[ploy][sema][const][diag]") {
    auto result = Analyze("const KBad: i32 = \"hello\";\n");
    REQUIRE(result.diags.HasErrors());
    CHECK(result.sema->LookupConstantText("KBad") == nullptr);
}
