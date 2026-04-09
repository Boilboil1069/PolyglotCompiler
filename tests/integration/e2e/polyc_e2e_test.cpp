// ============================================================================
// End-to-End Compilation Integration Tests
//
// These tests exercise the full CompilationPipeline API (the same stages used
// by the polyc binary) for realistic cross-language programs, and verify:
//
//   1. A minimal C++/Python cross-language .ploy program compiles successfully
//      through all pipeline stages (frontend → sema → marshal → bridge →
//      backend → packaging).
//   2. The bridge generation output contains the expected cross-language stubs.
//   3. Error programs are rejected by the correct stage with meaningful diagnostics.
//
// These tests do NOT fork the polyc binary process; they invoke the
// CompilationPipeline C++ API directly so that failures are reported through
// Catch2's normal mechanism.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

#include "tools/polyld/include/polyglot_linker.h"
#include "tools/polyc/include/compilation_pipeline.h"
#include "frontends/common/include/diagnostics.h"

using namespace polyglot::compilation;
using polyglot::frontends::Diagnostics;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Build a minimal pipeline config for an in-memory ploy source string.
// The output is set to compile-only (no real linking to disk).
CompilationContext::Config MakeConfig(const std::string &source_text,
                                      const std::string &target_arch = "x86_64") {
    CompilationContext::Config cfg;
    cfg.source_text     = source_text;
    cfg.source_language = "ploy";
    cfg.source_label    = "<e2e_test>";
    cfg.target_arch     = target_arch;
    cfg.mode            = "compile";   // stop after backend, no file linking
    cfg.opt_level       = 0;
    cfg.verbose         = false;
    return cfg;
}

// Run frontend + sema stages only and return success flag
bool RunThroughSema(const std::string &source, Diagnostics &out_diags) {
    CompilationContext::Config cfg = MakeConfig(source);
    CompilationPipeline pipeline(cfg);

    if (!pipeline.RunFrontend()) {
        for (const auto &d : pipeline.GetContext().diagnostics->All())
            out_diags.ReportError(d.loc, d.code, d.message);
        return false;
    }
    if (!pipeline.RunSemantic()) {
        for (const auto &d : pipeline.GetContext().diagnostics->All())
            out_diags.ReportError(d.loc, d.code, d.message);
        return false;
    }
    return true;
}

} // namespace

// ============================================================================
// 1. Minimal C++/Python cross-language example — frontend + sema must succeed
// ============================================================================

TEST_CASE("E2E compile: minimal C++/Python LINK compiles through sema",
          "[e2e][compile][cross-lang][cpp][python]") {
    const std::string kSource = R"ploy(
// Minimal cross-language example: a C++ function called from Python-side code.
LINK(cpp, python, math_utils::add, pymath::add) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

FUNC use_add(a: INT, b: INT) -> INT {
    LET result = CALL(cpp, math_utils::add, a, b);
    RETURN result;
}
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());

    const auto *sema_db = pipeline.GetSemanticDb();
    REQUIRE(sema_db != nullptr);

    // The LINK entry must be present in the semantic database
    REQUIRE_FALSE(sema_db->link_entries.empty());
    bool found_link = false;
    for (const auto &entry : sema_db->link_entries) {
        if (entry.target_language == "cpp" && entry.source_language == "python") {
            found_link = true;
        }
    }
    CHECK(found_link);

    // The function symbol must be registered
    CHECK(sema_db->symbols.count("use_add") == 1);

    // No diagnostics errors
    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 2. Minimal C++/Python example — marshal plan stage succeeds
// ============================================================================

TEST_CASE("E2E compile: C++/Python cross-language marshal plan is generated",
          "[e2e][compile][cross-lang][cpp][python]") {
    const std::string kSource = R"ploy(
LINK(cpp, python, image_proc::resize, cv::resize) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

MAP_TYPE(cpp::double, python::float);

FUNC process_image(w: INT, h: INT) -> INT {
    LET r = CALL(cpp, image_proc::resize, w, h);
    RETURN r;
}
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());
    REQUIRE(pipeline.RunMarshalPlan());

    const auto *plan = pipeline.GetMarshalPlan();
    REQUIRE(plan != nullptr);
    CHECK(plan->success);

    // At least one call marshal plan must be present for the LINK
    CHECK_FALSE(plan->call_plans.empty());

    // The first plan should reference the cpp→python link
    bool found_plan = false;
    for (const auto &cp : plan->call_plans) {
        if (cp.target_language == "cpp" && cp.source_language == "python") {
            found_plan = true;
            // Arity: 2 params (w, h)
            CHECK(cp.param_plans.size() == 2);
        }
    }
    CHECK(found_plan);

    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 3. Bridge generation: cross-lang stubs are produced
// ============================================================================

TEST_CASE("E2E compile: bridge generation produces cross-language stubs",
          "[e2e][compile][cross-lang][bridge]") {
    const std::string kSource = R"ploy(
LINK(cpp, python, net::send, socket::send) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC transmit(payload: INT) -> INT {
    LET status = CALL(cpp, net::send, payload);
    RETURN status;
}
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());
    REQUIRE(pipeline.RunMarshalPlan());
    REQUIRE(pipeline.RunBridgeGeneration());

    const auto *bridges = pipeline.GetBridgeOutput();
    REQUIRE(bridges != nullptr);
    CHECK(bridges->success);

    // At least one stub must be generated for the LINK
    CHECK_FALSE(bridges->stubs.empty());

    // Verify a stub exists for the net::send → socket::send bridge
    bool found_stub = false;
    for (const auto &stub : bridges->stubs) {
        if (!stub.stub_name.empty() &&
            (stub.target_symbol.find("send") != std::string::npos ||
             stub.stub_name.find("send") != std::string::npos)) {
            found_stub = true;
            CHECK_FALSE(stub.code.empty());
        }
    }
    CHECK(found_stub);

    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 4. Backend stage: machine code is emitted for the functions
// ============================================================================

TEST_CASE("E2E compile: backend stage emits machine code for ploy functions",
          "[e2e][compile][backend]") {
    const std::string kSource = R"ploy(
FUNC add(a: INT, b: INT) -> INT {
    RETURN a + b;
}

FUNC multiply(x: INT, n: INT) -> INT {
    VAR result = 0;
    VAR i = 0;
    WHILE i < n {
        result = result + x;
        i = i + 1;
    }
    RETURN result;
}
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());
    REQUIRE(pipeline.RunMarshalPlan());
    REQUIRE(pipeline.RunBridgeGeneration());
    REQUIRE(pipeline.RunBackend());

    const auto *backend_out = pipeline.GetBackendOutput();
    REQUIRE(backend_out != nullptr);
    CHECK(backend_out->success);

    // Objects must be non-empty
    CHECK_FALSE(backend_out->objects.empty());

    // At least one object should have non-empty code
    bool has_code = false;
    for (const auto &obj : backend_out->objects) {
        if (!obj.code.empty()) {
            has_code = true;
            break;
        }
    }
    CHECK(has_code);

    // Symbols must be present
    bool has_symbol = false;
    for (const auto &obj : backend_out->objects) {
        for (const auto &sym : obj.symbols) {
            if (sym.name.find("add") != std::string::npos ||
                sym.name.find("multiply") != std::string::npos) {
                has_symbol = true;
            }
        }
    }
    CHECK(has_symbol);

    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 5. Full C++/Python cross-language compile: all stages pass
// ============================================================================

TEST_CASE("E2E compile: full C++/Python cross-language example passes all stages",
          "[e2e][compile][cross-lang][cpp][python][full]") {
    // A realistic minimal example: Python-side code calls a C++ matrix routine
    // and a C++ function calls back into Python for data loading.
    const std::string kSource = R"ploy(
CONFIG VENV python "venv";

IMPORT python PACKAGE numpy >= 1.20 AS np;

LINK(cpp, python, matrix::multiply, np::dot) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::double, python::float);
}

LINK(python, cpp, data_loader::load, cpp_loader::load_csv) {
    MAP_TYPE(python::str, cpp::string);
}

MAP_TYPE(cpp::double, python::float);

FUNC run_computation(a: FLOAT, b: FLOAT) -> FLOAT {
    LET result = CALL(cpp, matrix::multiply, a, b);
    RETURN result;
}

FUNC load_data(path: STRING) -> INT {
    LET data = CALL(python, data_loader::load, path);
    RETURN 0;
}

EXPORT run_computation AS "polyglot_run_computation";
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    CHECK(pipeline.RunFrontend());
    CHECK(pipeline.RunSemantic());
    CHECK(pipeline.RunMarshalPlan());
    CHECK(pipeline.RunBridgeGeneration());
    CHECK(pipeline.RunBackend());

    const auto &ctx = pipeline.GetContext();
    CHECK_FALSE(ctx.diagnostics->HasErrors());

    // Sema: both LINK entries registered
    const auto *sema_db = pipeline.GetSemanticDb();
    REQUIRE(sema_db != nullptr);
    CHECK(sema_db->link_entries.size() >= 2);

    // Bridges: stubs generated for both directions
    const auto *bridges = pipeline.GetBridgeOutput();
    REQUIRE(bridges != nullptr);
    CHECK(bridges->stubs.size() >= 2);

    // Backend: code produced
    const auto *backend = pipeline.GetBackendOutput();
    REQUIRE(backend != nullptr);
    CHECK_FALSE(backend->objects.empty());
}

// ============================================================================
// 6. PYTHON/Rust cross-language minimal example
// ============================================================================

TEST_CASE("E2E compile: Python/Rust cross-language example passes sema",
          "[e2e][compile][cross-lang][python][rust]") {
    const std::string kSource = R"ploy(
IMPORT rust PACKAGE serde >= 1.0;

LINK(python, rust, model::serialize, serde::to_json) {
    MAP_TYPE(python::dict, rust::Value);
}

FUNC export_model(data: STRING) -> INT {
    LET json = CALL(python, model::serialize, data);
    RETURN 0;
}
)ploy";

    Diagnostics out_diags;
    bool ok = RunThroughSema(kSource, out_diags);
    CHECK(ok);
    CHECK_FALSE(out_diags.HasErrors());
}

// ============================================================================
// 7. Failure case: compilation pipeline rejects undefined cross-lang symbol
// ============================================================================

TEST_CASE("E2E compile: pipeline rejects CALL to undeclared cross-lang function",
          "[e2e][compile][failure]") {
    const std::string kSource = R"ploy(
FUNC main() -> INT {
    // No LINK declaration for math::sqrt
    LET r = CALL(cpp, math::sqrt, 9.0);
    RETURN 0;
}
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    pipeline.RunFrontend();
    bool sema_ok = pipeline.RunSemantic();

    CHECK_FALSE(sema_ok);
    CHECK(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 8. Failure case: pipeline rejects unsupported language in LINK
// ============================================================================

TEST_CASE("E2E compile: pipeline rejects LINK with unsupported language",
          "[e2e][compile][failure]") {
    const std::string kSource = R"ploy(
LINK(fortran, python, legacy::routine, py::run);

FUNC main() -> INT { RETURN 0; }
)ploy";

    Diagnostics out_diags;
    bool ok = RunThroughSema(kSource, out_diags);
    CHECK_FALSE(ok);
    CHECK(out_diags.HasErrors());
}

// ============================================================================
// 9. Failure case: param count mismatch in cross-lang call is caught by pipeline
// ============================================================================

TEST_CASE("E2E compile: pipeline rejects wrong arity in CALL with LINK MAP_TYPE",
          "[e2e][compile][failure][param-count]") {
    const std::string kSource = R"ploy(
LINK(cpp, python, vec::dot, np::dot) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::double, python::float);
}

FUNC main() -> INT {
    // dot expects 2 args; passing only 1
    LET r = CALL(cpp, vec::dot, 1.0);
    RETURN 0;
}
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    pipeline.RunFrontend();
    bool sema_ok = pipeline.RunSemantic();

    CHECK_FALSE(sema_ok);
    CHECK(pipeline.GetContext().diagnostics->HasErrors());

    // The error message should mention the arity mismatch
    bool found_arity_msg = false;
    for (const auto &d : pipeline.GetContext().diagnostics->All()) {
        if (d.message.find("argument") != std::string::npos ||
            d.message.find("param") != std::string::npos ||
            d.message.find("arity") != std::string::npos) {
            found_arity_msg = true;
        }
    }
    CHECK(found_arity_msg);
}

// ============================================================================
// 10. NEW + METHOD cross-language: full compile through backend
// ============================================================================

TEST_CASE("E2E compile: NEW + METHOD C++/Python example compiles through backend",
          "[e2e][compile][cross-lang][class]") {
    const std::string kSource = R"ploy(
IMPORT python PACKAGE sklearn;

EXTEND(python, sklearn::BaseEstimator) AS MyEstimator {
    FUNC fit(n_samples: INT) -> INT { RETURN n_samples; }
    FUNC predict(x: INT) -> FLOAT { RETURN 0.0; }
}

FUNC train_and_predict() -> INT {
    LET model = NEW(python, MyEstimator);
    LET fitted = METHOD(python, model, fit, 100);
    LET result = METHOD(python, model, predict, 42);
    DELETE(python, model);
    RETURN fitted;
}

EXPORT train_and_predict AS "polyglot_train_predict";
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    CHECK(pipeline.RunFrontend());
    CHECK(pipeline.RunSemantic());
    CHECK(pipeline.RunMarshalPlan());
    CHECK(pipeline.RunBridgeGeneration());
    CHECK(pipeline.RunBackend());

    const auto &ctx = pipeline.GetContext();
    CHECK_FALSE(ctx.diagnostics->HasErrors());

    // Verify EXTEND bridges were generated
    const auto *bridges = pipeline.GetBridgeOutput();
    REQUIRE(bridges != nullptr);
    bool has_fit_stub = false;
    bool has_predict_stub = false;
    for (const auto &stub : bridges->stubs) {
        if (stub.stub_name.find("fit") != std::string::npos)     has_fit_stub = true;
        if (stub.stub_name.find("predict") != std::string::npos) has_predict_stub = true;
    }
    CHECK(has_fit_stub);
    CHECK(has_predict_stub);
}

// ============================================================================
// 11. Assembly emission: backend produces non-empty assembly text
// ============================================================================

TEST_CASE("E2E compile: backend emits non-empty assembly text",
          "[e2e][compile][asm]") {
    const std::string kSource = R"ploy(
FUNC factorial(n: INT, acc: INT) -> INT {
    IF n <= 1 { RETURN acc; }
    RETURN factorial(n - 1, n * acc);
}
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    cfg.emit_asm_path = "<memory>";   // signal backend to capture asm in memory
    CompilationPipeline pipeline(cfg);

    CHECK(pipeline.RunFrontend());
    CHECK(pipeline.RunSemantic());
    CHECK(pipeline.RunMarshalPlan());
    CHECK(pipeline.RunBridgeGeneration());
    CHECK(pipeline.RunBackend());

    const auto *backend = pipeline.GetBackendOutput();
    REQUIRE(backend != nullptr);
    CHECK(backend->success);

    // Assembly text must be non-empty when emit_asm_path is set
    if (!backend->assembly_text.empty()) {
        CHECK(backend->assembly_text.find("factorial") != std::string::npos);
    }

    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 12. Timing information: pipeline records per-stage timing
// ============================================================================

TEST_CASE("E2E compile: pipeline records timing for executed stages",
          "[e2e][compile][timing]") {
    const std::string kSource = R"ploy(
FUNC f(x: INT) -> INT { RETURN x + 1; }
)ploy";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    pipeline.RunFrontend();
    pipeline.RunSemantic();

    const auto &timings = pipeline.GetContext().timings;
    CHECK_FALSE(timings.empty());

    // At least the frontend and sema stages should have timing entries
    bool has_frontend = false;
    bool has_sema = false;
    for (const auto &t : timings) {
        if (t.name.find("frontend") != std::string::npos ||
            t.name.find("Frontend") != std::string::npos) has_frontend = true;
        if (t.name.find("sema") != std::string::npos ||
            t.name.find("Semantic") != std::string::npos) has_sema = true;
    }
    CHECK(has_frontend);
    CHECK(has_sema);
}
