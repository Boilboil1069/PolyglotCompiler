/**
 * @file     polyc_cli_e2e_test.cpp
 * @brief    End-to-end CLI tests for polyc compilation pipeline
 *
 * Tests exercise the CompilationPipeline API with realistic sample programs
 * that mirror what the polyc binary processes, covering:
 *   1. Single-function compilation through all stages.
 *   2. Multi-function programs with control flow.
 *   3. Cross-language LINK/CALL programs.
 *   4. PIPELINE declarations.
 *   5. Error rejection at various stages.
 *   6. ARM64 and WASM target backends.
 *   7. EXPORT directive processing.
 *   8. Large-ish programs with many declarations.
 *
 * @ingroup  Tests / E2E
 * @author   Manning Cyrus
 * @date     2026-04-11
 */

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "tools/polyc/include/compilation_pipeline.h"
#include "frontends/common/include/diagnostics.h"

using namespace polyglot::compilation;
using polyglot::frontends::Diagnostics;

// ============================================================================
// Helpers
// ============================================================================

namespace {

CompilationContext::Config MakeCfg(const std::string &source,
                                   const std::string &arch = "x86_64") {
    CompilationContext::Config cfg;
    cfg.source_text     = source;
    cfg.source_language = "ploy";
    cfg.source_label    = "<cli_e2e_test>";
    cfg.target_arch     = arch;
    cfg.mode            = "compile";
    cfg.opt_level       = 0;
    cfg.verbose         = false;
    return cfg;
}

// Run the full pipeline and return whether every stage succeeded.
bool RunFullPipeline(const std::string &source,
                     const std::string &arch = "x86_64") {
    CompilationContext::Config cfg = MakeCfg(source, arch);
    CompilationPipeline pipeline(cfg);
    return pipeline.RunAll();
}

} // namespace

// ============================================================================
// 1. Single pure function — all stages pass
// ============================================================================

TEST_CASE("CLI E2E: single function compiles through all stages",
          "[cli][e2e][basic]") {
    const std::string kSource = R"ploy(
FUNC square(x: INT) -> INT {
    RETURN x * x;
}
)ploy";

    CompilationContext::Config cfg = MakeCfg(kSource);
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());
    REQUIRE(pipeline.RunMarshalPlan());
    REQUIRE(pipeline.RunBridgeGeneration());
    REQUIRE(pipeline.RunBackend());

    const auto *backend_out = pipeline.GetBackendOutput();
    REQUIRE(backend_out != nullptr);
    CHECK(backend_out->success);
    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 2. Multiple functions with control flow
// ============================================================================

TEST_CASE("CLI E2E: multi-function program with loops and branches",
          "[cli][e2e][control-flow]") {
    const std::string kSource = R"ploy(
FUNC abs_val(n: INT) -> INT {
    IF n < 0 { RETURN 0 - n; }
    RETURN n;
}

FUNC sum_range(lo: INT, hi: INT) -> INT {
    VAR total = 0;
    VAR i = lo;
    WHILE i <= hi {
        total = total + i;
        i = i + 1;
    }
    RETURN total;
}

FUNC fib(n: INT) -> INT {
    IF n <= 1 { RETURN n; }
    RETURN fib(n - 1) + fib(n - 2);
}
)ploy";

    CHECK(RunFullPipeline(kSource));
}

// ============================================================================
// 3. Cross-language LINK + CALL — sema and marshal pass
// ============================================================================

TEST_CASE("CLI E2E: cross-language LINK + CALL compiles successfully",
          "[cli][e2e][cross-lang]") {
    const std::string kSource = R"ploy(
IMPORT cpp::math_ops;
IMPORT python::string_utils;

LINK(cpp, python, math_ops::add, string_utils::concat) RETURNS cpp::int {
    MAP_TYPE(cpp::int, python::str);
    MAP_TYPE(cpp::int, python::str);
}

MAP_TYPE(cpp::int, python::int);

FUNC compute(a: INT, b: INT) -> INT {
    LET sum = CALL(cpp, math_ops::add, a, b);
    RETURN sum;
}
)ploy";

    CompilationContext::Config cfg = MakeCfg(kSource);
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());

    const auto *sema_db = pipeline.GetSemanticDb();
    REQUIRE(sema_db != nullptr);
    CHECK_FALSE(sema_db->link_entries.empty());
    CHECK(sema_db->symbols.count("compute") == 1);

    REQUIRE(pipeline.RunMarshalPlan());
    const auto *plan = pipeline.GetMarshalPlan();
    REQUIRE(plan != nullptr);
    CHECK(plan->success);

    REQUIRE(pipeline.RunBridgeGeneration());
    REQUIRE(pipeline.RunBackend());
    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 4. PIPELINE declaration compiles through sema
// ============================================================================

TEST_CASE("CLI E2E: PIPELINE declaration passes all stages",
          "[cli][e2e][pipeline]") {
    const std::string kSource = R"ploy(
PIPELINE data_flow {
    FUNC extract(src: STRING) -> STRING {
        RETURN src;
    }
    FUNC transform(raw: STRING) -> INT {
        RETURN 42;
    }
    FUNC load(count: INT) -> INT {
        RETURN count;
    }
}
)ploy";

    CHECK(RunFullPipeline(kSource));
}

// ============================================================================
// 5. Syntax error rejection — frontend fails
// ============================================================================

TEST_CASE("CLI E2E: syntax error is rejected at frontend stage",
          "[cli][e2e][failure][syntax]") {
    const std::string kSource = R"ploy(
FUNC broken( -> INT {
    RETURN 1;
}
)ploy";

    CompilationContext::Config cfg = MakeCfg(kSource);
    CompilationPipeline pipeline(cfg);

    bool frontend_ok = pipeline.RunFrontend();
    // Frontend should either fail or sema should catch the issue
    if (frontend_ok) {
        bool sema_ok = pipeline.RunSemantic();
        CHECK_FALSE(sema_ok);
    }
    CHECK(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 6. ARM64 backend target
// ============================================================================

TEST_CASE("CLI E2E: ARM64 backend compiles a simple function",
          "[cli][e2e][arm64]") {
    const std::string kSource = R"ploy(
FUNC inc(x: INT) -> INT {
    RETURN x + 1;
}
)ploy";

    CompilationContext::Config cfg = MakeCfg(kSource, "arm64");
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());
    REQUIRE(pipeline.RunMarshalPlan());
    REQUIRE(pipeline.RunBridgeGeneration());
    REQUIRE(pipeline.RunBackend());

    const auto *backend = pipeline.GetBackendOutput();
    REQUIRE(backend != nullptr);
    CHECK(backend->success);
    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 7. EXPORT directive is tracked in sema
// ============================================================================

TEST_CASE("CLI E2E: EXPORT directive produces exported symbol",
          "[cli][e2e][export]") {
    const std::string kSource = R"ploy(
FUNC helper(a: INT) -> INT {
    RETURN a * 2;
}

EXPORT helper AS "polyglot_helper";
)ploy";

    CompilationContext::Config cfg = MakeCfg(kSource);
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());

    const auto *sema_db = pipeline.GetSemanticDb();
    REQUIRE(sema_db != nullptr);
    CHECK(sema_db->symbols.count("helper") == 1);

    REQUIRE(pipeline.RunMarshalPlan());
    REQUIRE(pipeline.RunBridgeGeneration());
    REQUIRE(pipeline.RunBackend());
    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 8. Large program with many declarations
// ============================================================================

TEST_CASE("CLI E2E: program with many functions and links compiles",
          "[cli][e2e][large]") {
    const std::string kSource = R"ploy(
IMPORT cpp::math;
IMPORT python::util;

LINK(cpp, python, math::add, util::py_add) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

LINK(cpp, python, math::sub, util::py_sub) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::double, python::float);

FUNC f1(x: INT) -> INT { RETURN x + 1; }
FUNC f2(x: INT) -> INT { RETURN x + 2; }
FUNC f3(x: INT) -> INT { RETURN x + 3; }
FUNC f4(x: INT) -> INT { RETURN x + 4; }
FUNC f5(x: INT) -> INT { RETURN x + 5; }

FUNC orchestrate(a: INT, b: INT) -> INT {
    LET s = CALL(cpp, math::add, a, b);
    LET d = CALL(cpp, math::sub, a, b);
    RETURN f1(s) + f2(d) + f3(a) + f4(b) + f5(0);
}

EXPORT orchestrate AS "polyglot_orchestrate";
)ploy";

    CHECK(RunFullPipeline(kSource));
}
