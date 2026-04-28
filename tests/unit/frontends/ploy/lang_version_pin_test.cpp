// ============================================================================
// Unit tests for ploy LANG / WITH LANG / @LANG version-pinning syntax.
//
// Covers:
//   1. `LANG <lang> = <version>;` module-level pragma is parsed and is the
//      version that subsequent cross-language calls inherit.
//   2. `WITH LANG (lang=ver, ...)` block scopes its pins to its body and
//      pops them on exit.
//   3. `@LANG (lang=ver) <stmt>` annotates a single statement only.
//   4. Inner pin shadows outer pin; outer is restored after inner exits.
//   5. Pins flow into `CrossLangCallExpression::lang_version_pin` and from
//      there into the `CrossLangCallDescriptor::lang_version` produced by
//      lowering.
//   6. `LinkEntry::lang_version` is stamped with the enclosing pin so the
//      bridge / linker pipeline can carry it through to polyld.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "middle/include/ir/ir_context.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::CrossLangCallDescriptor;
using polyglot::ploy::CrossLangCallExpression;
using polyglot::ploy::LangAnnotation;
using polyglot::ploy::LangPragma;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyLowering;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::WithLangBlock;

namespace {

// Parse a .ploy source string. Returns the analyzed module on success.
struct PipelineResult {
    std::shared_ptr<polyglot::ploy::Module> module;
    bool parse_ok{false};
    bool sema_ok{false};
    Diagnostics diags;
    PloySema sema;

    PipelineResult() : sema(diags, PloySemaOptions{}) {}
};

std::unique_ptr<PipelineResult> RunPipeline(const std::string &code) {
    auto r = std::make_unique<PipelineResult>();
    PloyLexer lexer(code, "<test>");
    PloyParser parser(lexer, r->diags);
    parser.ParseModule();
    r->module = parser.TakeModule();
    r->parse_ok = (r->module != nullptr) && !r->diags.HasErrors();
    if (r->parse_ok) {
        r->sema_ok = r->sema.Analyze(r->module);
    }
    return r;
}

// Walk the analyzed module and collect every CrossLangCallExpression.
// `Module` is a plain struct (not derived from AstNode), so we walk its
// `declarations` vector directly. Statement nodes that contain other
// statements / expressions are unwrapped via dynamic_pointer_cast.
void CollectCallsInStatement(const std::shared_ptr<polyglot::ploy::Statement> &stmt,
                             std::vector<std::shared_ptr<CrossLangCallExpression>> &out);

void CollectCallsInExpression(const std::shared_ptr<polyglot::ploy::Expression> &expr,
                              std::vector<std::shared_ptr<CrossLangCallExpression>> &out) {
    if (!expr) return;
    if (auto call = std::dynamic_pointer_cast<CrossLangCallExpression>(expr)) {
        out.push_back(call);
    }
}

void CollectCallsInStatement(const std::shared_ptr<polyglot::ploy::Statement> &stmt,
                             std::vector<std::shared_ptr<CrossLangCallExpression>> &out) {
    if (!stmt) return;
    if (auto es = std::dynamic_pointer_cast<polyglot::ploy::ExprStatement>(stmt)) {
        CollectCallsInExpression(es->expr, out);
        return;
    }
    if (auto with_lang = std::dynamic_pointer_cast<WithLangBlock>(stmt)) {
        for (const auto &s : with_lang->body) CollectCallsInStatement(s, out);
        return;
    }
    if (auto anno = std::dynamic_pointer_cast<LangAnnotation>(stmt)) {
        CollectCallsInStatement(anno->target, out);
        return;
    }
}

void CollectCrossLangCalls(const std::shared_ptr<polyglot::ploy::Module> &mod,
                           std::vector<std::shared_ptr<CrossLangCallExpression>> &out) {
    if (!mod) return;
    for (const auto &s : mod->declarations) CollectCallsInStatement(s, out);
}

} // namespace

// ============================================================================
// LangPragma — module-level pin
// ============================================================================

TEST_CASE("LANG pragma stamps version on subsequent cross-lang calls",
          "[ploy][lang][pin][pragma]") {
    const std::string code = R"PLOY(
LANG python = "3.11";

LINK(python, ploy, math::sqrt, host_sqrt);
CALL(python, math::sqrt, 4.0);
)PLOY";
    auto r = RunPipeline(code);
    REQUIRE(r->parse_ok);
    REQUIRE(r->sema_ok);

    std::vector<std::shared_ptr<CrossLangCallExpression>> calls;
    CollectCrossLangCalls(r->module, calls);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]->language == "python");
    CHECK(calls[0]->lang_version_pin == "3.11");
}

TEST_CASE("LANG pragma only affects matching language",
          "[ploy][lang][pin][pragma]") {
    const std::string code = R"PLOY(
LANG python = "3.11";

LINK(cpp, ploy, compute, host_compute);
LINK(python, ploy, len, host_len);
CALL(cpp, compute, 1);
CALL(python, len, "abc");
)PLOY";
    auto r = RunPipeline(code);
    REQUIRE(r->parse_ok);
    REQUIRE(r->sema_ok);

    std::vector<std::shared_ptr<CrossLangCallExpression>> calls;
    CollectCrossLangCalls(r->module, calls);
    REQUIRE(calls.size() == 2);
    // The cpp call has no matching pragma → empty pin.
    CHECK(calls[0]->language == "cpp");
    CHECK(calls[0]->lang_version_pin.empty());
    // The python call inherits the module pragma.
    CHECK(calls[1]->language == "python");
    CHECK(calls[1]->lang_version_pin == "3.11");
}

// ============================================================================
// WITH LANG block — scoped pin
// ============================================================================

TEST_CASE("WITH LANG block scopes pins to body only",
          "[ploy][lang][pin][with]") {
    const std::string code = R"PLOY(
LINK(python, ploy, math::sqrt, host_sqrt);
LINK(cpp, ploy, std::abs, host_abs);
LINK(python, ploy, len, host_len);
WITH LANG (python="3.12", cpp="c++23") {
    CALL(python, math::sqrt, 4.0);
    CALL(cpp, std::abs, -1);
}
CALL(python, len, "abc");
)PLOY";
    auto r = RunPipeline(code);
    REQUIRE(r->parse_ok);
    REQUIRE(r->sema_ok);

    std::vector<std::shared_ptr<CrossLangCallExpression>> calls;
    CollectCrossLangCalls(r->module, calls);
    REQUIRE(calls.size() == 3);
    // Inside the WITH LANG block.
    CHECK(calls[0]->lang_version_pin == "3.12");
    CHECK(calls[1]->lang_version_pin == "c++23");
    // Outside the block — pin must have been popped.
    CHECK(calls[2]->lang_version_pin.empty());
}

TEST_CASE("WITH LANG inner block shadows outer pragma",
          "[ploy][lang][pin][with]") {
    const std::string code = R"PLOY(
LANG python = "3.11";

LINK(python, ploy, before, host_before);
LINK(python, ploy, inside, host_inside);
LINK(python, ploy, after, host_after);
CALL(python, before, 1);
WITH LANG (python="3.12") {
    CALL(python, inside, 2);
}
CALL(python, after, 3);
)PLOY";
    auto r = RunPipeline(code);
    REQUIRE(r->parse_ok);
    REQUIRE(r->sema_ok);

    std::vector<std::shared_ptr<CrossLangCallExpression>> calls;
    CollectCrossLangCalls(r->module, calls);
    REQUIRE(calls.size() == 3);
    CHECK(calls[0]->lang_version_pin == "3.11"); // outer pragma
    CHECK(calls[1]->lang_version_pin == "3.12"); // inner shadow
    CHECK(calls[2]->lang_version_pin == "3.11"); // outer restored
}

// ============================================================================
// @LANG annotation — single-statement pin
// ============================================================================

TEST_CASE("@LANG annotation pins exactly one statement",
          "[ploy][lang][pin][annotation]") {
    const std::string code = R"PLOY(
LANG python = "3.11";

LINK(python, ploy, first, host_first);
LINK(python, ploy, annotated, host_annotated);
LINK(python, ploy, third, host_third);
CALL(python, first, 1);
@LANG (python="3.12")
CALL(python, annotated, 2);
CALL(python, third, 3);
)PLOY";
    auto r = RunPipeline(code);
    REQUIRE(r->parse_ok);
    REQUIRE(r->sema_ok);

    std::vector<std::shared_ptr<CrossLangCallExpression>> calls;
    CollectCrossLangCalls(r->module, calls);
    REQUIRE(calls.size() == 3);
    CHECK(calls[0]->lang_version_pin == "3.11");
    CHECK(calls[1]->lang_version_pin == "3.12"); // annotated only
    CHECK(calls[2]->lang_version_pin == "3.11"); // back to module pragma
}

// ============================================================================
// End-to-end: pin propagates into CrossLangCallDescriptor via lowering
// ============================================================================

TEST_CASE("Lowering propagates AST pin into CrossLangCallDescriptor::lang_version",
          "[ploy][lang][pin][lowering]") {
    const std::string code = R"PLOY(
LANG python = "3.11";

LINK(python, ploy, math::sqrt, host_sqrt);
@LANG (python="3.12")
LINK(python, ploy, len, host_len);
CALL(python, math::sqrt, 4.0);
@LANG (python="3.12")
CALL(python, len, "abc");
)PLOY";
    auto r = RunPipeline(code);
    REQUIRE(r->parse_ok);
    REQUIRE(r->sema_ok);

    polyglot::ir::IRContext ir_ctx;
    PloyLowering lowering(ir_ctx, r->diags, r->sema);
    REQUIRE(lowering.Lower(r->module));

    const auto &descs = lowering.CallDescriptors();
    // Each LINK becomes one bridge descriptor; the per-LINK pin must reach
    // CrossLangCallDescriptor::lang_version so the linker can emit the
    // correct ABI shim for that exact version.
    std::vector<CrossLangCallDescriptor> py_calls;
    for (const auto &d : descs) {
        if (d.source_language == "python") py_calls.push_back(d);
    }
    REQUIRE(py_calls.size() == 2);
    // Order is source order: math::sqrt LINK is first (3.11), len is second (3.12).
    CHECK(py_calls[0].lang_version == "3.11");
    CHECK(py_calls[1].lang_version == "3.12");
}

// ============================================================================
// LinkEntry stamping — bridge / linker pipeline carries the version
// ============================================================================

TEST_CASE("LINK declarations inside LANG scope inherit the pinned version",
          "[ploy][lang][pin][link]") {
    const std::string code = R"PLOY(
LANG python = "3.11";

LINK(ploy, python, my_sqrt, math::sqrt) {
}
WITH LANG (python="3.12") {
    LINK(ploy, python, my_len, len) {
    }
}
)PLOY";
    auto r = RunPipeline(code);
    REQUIRE(r->parse_ok);
    REQUIRE(r->sema_ok);

    const auto &links = r->sema.Links();
    REQUIRE(links.size() == 2);
    // Order in `links_` matches source order.
    CHECK(links[0].source_symbol == "math::sqrt");
    CHECK(links[0].lang_version == "3.11");
    CHECK(links[1].source_symbol == "len");
    CHECK(links[1].lang_version == "3.12");
}
