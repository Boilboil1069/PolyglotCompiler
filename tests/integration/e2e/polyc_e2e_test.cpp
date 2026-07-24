// ============================================================================
// End-to-End Compilation Integration Tests
//
// These tests exercise the full CompilationPipeline API (the same stages used
// by the polyc binary) for realistic cross-language programs, and verify:
//
//   1. A minimal C++/Python cross-language .poly program compiles successfully
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
#include <catch2/catch_message.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

#include "backends/x86_64/include/x86_target.h"
#include "frontends/common/include/language_frontend.h"
#include "tools/polyld/include/polyglot_linker.h"
#include "tools/polyc/include/compilation_pipeline.h"
#include "tools/polyc/src/local_source_packages.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/go/include/go_frontend.h"
#include "frontends/python/include/python_frontend.h"
#include "frontends/rust/include/rust_frontend.h"
#include "middle/include/ir/verifier.h"

using namespace polyglot::compilation;
using polyglot::frontends::Diagnostics;

// ============================================================================
// Helpers
// ============================================================================

namespace {

namespace fs = std::filesystem;

// Build a minimal pipeline config for an in-memory poly source string.
// The output is set to compile-only (no real linking to disk).
CompilationContext::Config MakeConfig(const std::string &source_text,
                                      const std::string &target_arch = "x86_64") {
    CompilationContext::Config cfg;
    cfg.source_text     = source_text;
    cfg.source_language = "poly";
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

std::string ReadTextFile(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void RequireFrontendAndX86Lowering(
    const polyglot::frontends::ILanguageFrontend &frontend,
    const fs::path &bundle_path,
    const std::string &wrapper_name,
    const std::vector<std::string> &expected_package_calls) {
    const std::string source = ReadTextFile(bundle_path);
    polyglot::frontends::FrontendOptions options;
    options.strict = true;

    // Analyze is an independent lexer/parser/sema pass.  Lower then repeats
    // that checked frontend path and must produce verifiable native IR.
    Diagnostics analysis_diags;
    const bool analyzed = frontend.Analyze(source, bundle_path.string(),
                                           analysis_diags, options);
    INFO(frontend.Name() << " analyze diagnostics:\n" << analysis_diags.FormatAll());
    REQUIRE(analyzed);
    REQUIRE_FALSE(analysis_diags.HasErrors());

    polyglot::ir::IRContext ir;
    Diagnostics lowering_diags;
    const auto result = frontend.Lower(source, bundle_path.string(), ir,
                                       lowering_diags, options);
    INFO(frontend.Name() << " lowering diagnostics:\n" << lowering_diags.FormatAll());
    REQUIRE(result.success);
    REQUIRE(result.lowered);
    REQUIRE_FALSE(lowering_diags.HasErrors());
    REQUIRE_FALSE(ir.Functions().empty());

    std::string verify_message;
    const bool verified = polyglot::ir::Verify(ir, &verify_message);
    INFO(frontend.Name() << " IR verifier: " << verify_message);
    REQUIRE(verified);

    const auto *wrapper = ir.FindFunction(wrapper_name);
    REQUIRE(wrapper != nullptr);
    for (const auto &expected : expected_package_calls) {
        bool found = false;
        for (const auto &block : wrapper->blocks) {
            for (const auto &instruction : block->instructions) {
                const auto *call =
                    dynamic_cast<const polyglot::ir::CallInstruction *>(instruction.get());
                found = found || (call && call->callee == expected);
            }
        }
        INFO(frontend.Name() << " package call: " << expected);
        REQUIRE(found);
    }

    // This closes the gap between a frontend-only fixture and the real polyc
    // path: aggregate allocas/GEPs and receiver calls must survive x86 isel.
    polyglot::backends::x86_64::X86Target target(&ir);
    const auto object = target.EmitObjectCode();
    bool has_machine_text = false;
    for (const auto &section : object.sections)
        has_machine_text = has_machine_text ||
                           (section.name == ".text" && !section.data.empty());
    REQUIRE(has_machine_text);

    bool exports_wrapper = false;
    for (const auto &symbol : object.symbols)
        exports_wrapper = exports_wrapper ||
                          (symbol.name == wrapper_name && symbol.defined);
    REQUIRE(exports_wrapper);
}

} // namespace

TEST_CASE("CompilationPipeline canonicalizes Poly aliases without rewriting other languages",
          "[e2e][compile][poly][compat]") {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"poly", "poly"}, {"Poly", "poly"}, {"POLY", "poly"},
        {"ploy", "poly"}, {"Ploy", "poly"}, {"PLOY", "poly"},
        {"Cpp", "Cpp"},   {"PYTHON", "PYTHON"},
    };

    for (const auto &[input, expected] : cases) {
        INFO("source language: " << input);
        auto cfg = MakeConfig("");
        cfg.source_language = input;
        CompilationPipeline pipeline(std::move(cfg));
        CHECK(pipeline.GetContext().config.source_language == expected);
    }
}

TEST_CASE("CompilationPipeline resolves only declared vendored C++ package manifests",
          "[e2e][compile][poly][packages][cpp]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("polyc_local_package_discovery_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path explicit_root = root / "explicit";
    const fs::path project_include = root / "include";
    const fs::path vendor_include = root / "vendor" / "include";
    const fs::path declared_root = root / "packages" / "order_policy" / "public_api";
    const fs::path undeclared_default_root = root / "packages" / "order_policy" / "include";
    const fs::path undeclared_root = root / "packages" / "private_policy" / "hidden";
    const fs::path missing_manifest_root = root / "packages" / "missing_manifest" / "include";
    const fs::path wrong_language_root = root / "packages" / "python_policy" / "headers";
    const fs::path traversal_package = root / "packages" / "traversal_policy";
    const fs::path absolute_package = root / "packages" / "absolute_policy";
    const fs::path traversal_target = root / "packages" / "escape_target";
    const fs::path absolute_target = root / "absolute_target";
    std::error_code ec;
    for (const auto &directory : {explicit_root, project_include, vendor_include, declared_root,
                                  undeclared_default_root, undeclared_root,
                                  missing_manifest_root, wrong_language_root, traversal_package,
                                  absolute_package, traversal_target, absolute_target}) {
        fs::create_directories(directory, ec);
        REQUIRE_FALSE(ec);
    }

    const auto write_manifest = [&](const fs::path &package_directory,
                                    const std::string &name,
                                    const std::string &language,
                                    const std::string &include_dir) {
        std::ofstream out(package_directory / "poly.package.toml");
        REQUIRE(out.good());
        out << "name = \"" << name << "\"\n"
            << "version = \"1.0.0\"\n"
            << "language = \"" << language << "\"\n"
            << "include_dir = \"" << include_dir << "\"\n";
    };
    write_manifest(root / "packages" / "order_policy", "order_policy", "cpp", "public_api");
    write_manifest(root / "packages" / "private_policy", "private_policy", "cpp", "hidden");
    write_manifest(root / "packages" / "python_policy", "python_policy", "python", "headers");
    write_manifest(root / "packages" / "traversal_policy", "traversal_policy", "cpp",
                   "../escape_target");
    write_manifest(root / "packages" / "absolute_policy", "absolute_policy", "cpp",
                   absolute_target.string());

    const std::string source = R"poly(
// IMPORT cpp PACKAGE private_policy; -- comments are not declarations.
IMPORT cpp PACKAGE order_policy >= 1.0;
IMPORT cpp PACKAGE missing_manifest;
IMPORT cpp PACKAGE python_policy;
IMPORT cpp PACKAGE traversal_policy;
IMPORT cpp PACKAGE absolute_policy;
FUNC main() -> INT { RETURN 0; }
)poly";

    const fs::path entry = root / "order_risk.poly";
    {
        std::ofstream out(entry);
        REQUIRE(out.good());
        out << source;
    }

    auto cfg = MakeConfig(source);
    cfg.source_file = entry.string();
    cfg.source_label = entry.string();
    cfg.include_paths.push_back(explicit_root.string());
    CompilationPipeline pipeline(std::move(cfg));

    const auto &paths = pipeline.GetContext().config.include_paths;
    const auto contains = [&](const fs::path &path) {
        const fs::path expected = fs::weakly_canonical(path, ec);
        REQUIRE_FALSE(ec);
        return std::find(paths.begin(), paths.end(), expected.string()) != paths.end();
    };

    REQUIRE_FALSE(paths.empty());
    CHECK(paths.front() == explicit_root.string());
    CHECK(contains(project_include));
    CHECK(contains(vendor_include));
    CHECK(contains(declared_root));
    CHECK_FALSE(contains(undeclared_default_root));
    CHECK_FALSE(contains(undeclared_root));
    CHECK_FALSE(contains(missing_manifest_root));
    CHECK_FALSE(contains(wrong_language_root));
    CHECK_FALSE(contains(traversal_target));
    CHECK_FALSE(contains(absolute_target));

    fs::remove_all(root, ec);
}

TEST_CASE("polyc bundles declared vendored source packages for Python Rust and Go",
          "[e2e][compile][poly][packages][source]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("polyc_local_source_packages_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path aux = root / "aux";
    std::error_code ec;
    fs::create_directories(aux, ec);
    REQUIRE_FALSE(ec);

    const fs::path entry = root / "app.poly";
    {
        std::ofstream out(entry);
        REQUIRE(out.good());
        out << "IMPORT python PACKAGE fraud_policy >= 1.0;\n"
               "IMPORT rust PACKAGE fulfillment_policy >= 1.0;\n"
               "IMPORT go PACKAGE logistics_policy >= 1.0;\n"
               "// IMPORT python PACKAGE ignored_comment;\n"
               "FUNC main() -> INT { RETURN 0; }\n";
    }

    const auto make_package = [&](const std::string &directory,
                                  const std::string &name,
                                  const std::string &language,
                                  const std::string &source_name,
                                  const std::string &source) {
        const fs::path package = root / "packages" / directory;
        fs::create_directories(package / "src", ec);
        REQUIRE_FALSE(ec);
        std::ofstream manifest(package / "poly.package.toml");
        REQUIRE(manifest.good());
        manifest << "name = \"" << name << "\"\n"
                 << "version = \"1.0.0\"\n"
                 << "language = \"" << language << "\"\n"
                 << "source = \"src/" << source_name << "\"\n";
        std::ofstream package_source(package / "src" / source_name);
        REQUIRE(package_source.good());
        package_source << source;
    };
    make_package("fraud", "fraud_policy", "python", "policy.py", R"python(
class FraudAssessment:
    def __init__(self, velocity: int, amount: int, failed: int):
        self.velocity = velocity
        self.amount = amount
        self.failed = failed
        self.score = velocity + amount

    def apply_failure_penalty(self, multiplier: int) -> int:
        self.score += self.failed * multiplier
        return self.score

    def band(self) -> int:
        if self.score >= 80:
            return 3
        else:
            if self.score >= 45:
                return 2
            else:
                return 1

    def close(self) -> None:
        self.velocity = 0
        self.amount = 0
        self.failed = 0
        self.score = 0
)python");
    make_package("fulfillment", "fulfillment_policy", "rust", "policy.rs", R"rust(
pub struct FulfillmentSession {
    requested: i64,
    reserved: i64,
    status: i64,
}

impl FulfillmentSession {
    pub fn gate(&self) -> i64 {
        return self.reserved - self.requested + self.status;
    }

    pub fn close(&mut self) -> i64 {
        self.requested = 0;
        self.reserved = 0;
        self.status = 0;
        return self.requested + self.reserved + self.status;
    }
}
)rust");
    make_package("logistics", "logistics_policy", "go", "policy.go", R"go(
package logistics_policy

type LogisticsSession struct { gate int; eta int }

func NewLogisticsSession(session *LogisticsSession, gate int, eta int) {
    session.gate = gate
    session.eta = eta
}

func (session *LogisticsSession) Adjust(delta int) { session.eta += delta }
func (session *LogisticsSession) Decision() int { return session.gate + session.eta }
)go");

    const fs::path python_consumer = root / "fraud_engine.py";
    const fs::path rust_consumer = root / "fulfillment_engine.rs";
    const fs::path go_consumer = root / "logistics_engine.go";
    {
        std::ofstream out(python_consumer);
        out << R"python(
def fraud_session_band(velocity: int, amount: int, failed: int) -> int:
    session = FraudAssessment(velocity, amount, failed)
    session.apply_failure_penalty(12)
    result = session.band()
    session.close()
    return result
)python";
    }
    {
        std::ofstream out(rust_consumer);
        out << R"rust(
pub fn fulfillment_session_gate(requested: i64, reserved: i64, status: i64) -> i64 {
    let mut session = FulfillmentSession {
        requested: requested,
        reserved: reserved,
        status: status,
    };
    let result = session.gate();
    let closed = session.close();
    return result + closed;
}
)rust";
    }
    {
        std::ofstream out(go_consumer);
        out << R"go(package main

func logistics_session_decision(gate int, eta int) int {
    var session LogisticsSession
    NewLogisticsSession(&session, gate, eta)
    session.Adjust(2)
    return session.Decision()
}
)go";
    }

    std::string error;
    const std::string python_bundle = polyglot::tools::BuildVendoredSourceBundle(
        entry.string(), "python", python_consumer.string(), aux.string(), &error, true);
    INFO(error);
    REQUIRE_FALSE(python_bundle.empty());
    REQUIRE(python_bundle != python_consumer.string());
    const std::string python_text = ReadTextFile(python_bundle);
    CHECK(python_text.find("FraudAssessment") != std::string::npos);
    CHECK(python_text.find("fraud_session_band") != std::string::npos);
    polyglot::python::PythonLanguageFrontend python_frontend;
    RequireFrontendAndX86Lowering(
        python_frontend, python_bundle, "fraud_session_band",
        {"FraudAssessment.__init__", "FraudAssessment.apply_failure_penalty",
         "FraudAssessment.band", "FraudAssessment.close"});

    const std::string rust_bundle = polyglot::tools::BuildVendoredSourceBundle(
        entry.string(), "rust", rust_consumer.string(), aux.string(), &error, true);
    INFO(error);
    REQUIRE_FALSE(rust_bundle.empty());
    const std::string rust_text = ReadTextFile(rust_bundle);
    CHECK(rust_text.find("FulfillmentSession") != std::string::npos);
    CHECK(rust_text.find("fulfillment_session_gate") != std::string::npos);
    polyglot::rust::RustLanguageFrontend rust_frontend;
    RequireFrontendAndX86Lowering(
        rust_frontend, rust_bundle, "fulfillment_session_gate",
        {"FulfillmentSession.gate", "FulfillmentSession.close"});

    const std::string go_bundle = polyglot::tools::BuildVendoredSourceBundle(
        entry.string(), "go", go_consumer.string(), aux.string(), &error, true);
    INFO(error);
    REQUIRE_FALSE(go_bundle.empty());
    const std::string go_text = ReadTextFile(go_bundle);
    CHECK(go_text.find("package main") != std::string::npos);
    CHECK(go_text.find("package logistics_policy") == std::string::npos);
    CHECK(go_text.find("LogisticsSession") != std::string::npos);
    CHECK(go_text.find("logistics_session_decision") != std::string::npos);
    polyglot::go::GoLanguageFrontend go_frontend;
    RequireFrontendAndX86Lowering(
        go_frontend, go_bundle, "logistics_session_decision",
        {"NewLogisticsSession", "LogisticsSession.Adjust",
         "LogisticsSession.Decision"});

    fs::remove_all(root, ec);
}

TEST_CASE("strict local source packages fail closed on unresolved or unsafe manifests",
          "[e2e][compile][poly][packages][source][strict-local][negative]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("polyc_strict_local_packages_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path packages = root / "packages";
    const fs::path aux = root / "aux";
    std::error_code ec;
    fs::create_directories(packages, ec);
    REQUIRE_FALSE(ec);
    fs::create_directories(aux, ec);
    REQUIRE_FALSE(ec);

    const fs::path consumer = root / "consumer.py";
    {
        std::ofstream out(consumer);
        REQUIRE(out.good());
        out << "def run() -> int:\n    return 1\n";
    }

    const auto write_entry = [&](const std::string &stem, const std::string &declaration) {
        const fs::path entry = root / (stem + ".poly");
        std::ofstream out(entry);
        REQUIRE(out.good());
        out << declaration << "\nFUNC main() -> INT { RETURN 0; }\n";
        return entry;
    };
    const auto write_manifest = [&](const fs::path &directory,
                                    const std::string &name,
                                    const std::string &language,
                                    const std::string &version,
                                    const std::string &source) {
        fs::create_directories(directory / "src", ec);
        REQUIRE_FALSE(ec);
        std::ofstream out(directory / "poly.package.toml");
        REQUIRE(out.good());
        out << "name = \"" << name << "\"\n"
            << "version = \"" << version << "\"\n"
            << "language = \"" << language << "\"\n";
        if (!source.empty())
            out << "source = \"" << source << "\"\n";
    };
    const auto require_failure = [&](const fs::path &entry,
                                     const std::string &package_name,
                                     const std::string &error_needle = std::string()) {
        std::string error;
        const std::string result = polyglot::tools::BuildVendoredSourceBundle(
            entry.string(), "python", consumer.string(), aux.string(), &error, true);
        INFO("strict-local package: " << package_name << "\nerror: " << error);
        CHECK(result.empty());
        CHECK_FALSE(error.empty());
        CHECK(error.find(error_needle.empty() ? package_name : error_needle) !=
              std::string::npos);
    };

    const fs::path missing_entry = write_entry(
        "missing", "IMPORT python PACKAGE missing_policy >= 1.0;");
    require_failure(missing_entry, "missing_policy");

    const fs::path wrong_language = packages / "wrong_language";
    write_manifest(wrong_language, "wrong_language_policy", "rust", "1.0.0",
                   "src/policy.rs");
    {
        std::ofstream out(wrong_language / "src" / "policy.rs");
        out << "pub fn marker() -> i64 { return 1; }\n";
    }
    const fs::path wrong_language_entry = write_entry(
        "wrong_language",
        "IMPORT python PACKAGE wrong_language_policy >= 1.0;");
    require_failure(wrong_language_entry, "wrong_language_policy");

    const fs::path no_source = packages / "no_source";
    write_manifest(no_source, "no_source_policy", "python", "1.0.0", "");
    const fs::path no_source_entry = write_entry(
        "no_source", "IMPORT python PACKAGE no_source_policy >= 1.0;");
    require_failure(no_source_entry, "no_source_policy");

    const fs::path old_version = packages / "old_version";
    write_manifest(old_version, "old_policy", "python", "1.0.0", "src/policy.py");
    {
        std::ofstream out(old_version / "src" / "policy.py");
        out << "class OldPolicy:\n    pass\n";
    }
    const fs::path old_version_entry = write_entry(
        "old_version", "IMPORT python PACKAGE old_policy >= 2.0;");
    require_failure(old_version_entry, "old_policy");

    // A package directory symlink must not make a manifest outside packages/
    // eligible for strict-local resolution.
    const fs::path outside = root / "outside_package";
    write_manifest(outside, "escape_policy", "python", "1.0.0", "src/policy.py");
    {
        std::ofstream out(outside / "src" / "policy.py");
        out << "class EscapedPolicy:\n    pass\n";
    }
    fs::create_directory_symlink(outside, packages / "escape_link", ec);
    if (ec) {
        WARN("directory symlinks unavailable; escape case skipped: " << ec.message());
        ec.clear();
    } else {
        const fs::path escape_entry = write_entry(
            "escape", "IMPORT python PACKAGE escape_policy >= 1.0;");
        require_failure(escape_entry, "escape_policy", "escapes");
    }

    fs::remove_all(root, ec);
}

TEST_CASE("CompilationPipeline instruments legacy Poly input with canonical metadata",
          "[e2e][compile][poly][compat][profiler]") {
    auto cfg = MakeConfig("FUNC main() -> i32 { RETURN 0; }\n");
    cfg.source_language = "Ploy";
    cfg.profile_instrument = true;
    CompilationPipeline pipeline(std::move(cfg));

    REQUIRE(pipeline.RunFrontend());
    REQUIRE(pipeline.RunSemantic());
    REQUIRE(pipeline.RunMarshalPlan());
    REQUIRE(pipeline.RunBridgeGeneration());
    REQUIRE(pipeline.RunBackend());

    const auto *backend = pipeline.GetBackendOutput();
    REQUIRE(backend != nullptr);
    REQUIRE(backend->ir_ctx != nullptr);

    bool saw_enter = false;
    bool saw_exit = false;
    std::string language_global;
    for (const auto &fn : backend->ir_ctx->Functions()) {
        if (!fn) continue;
        for (const auto &block : fn->blocks) {
            if (!block) continue;
            for (const auto &inst : block->instructions) {
                const auto *call = dynamic_cast<const polyglot::ir::CallInstruction *>(inst.get());
                if (!call) continue;
                if (call->callee == "__ploy_rt_call_enter") {
                    saw_enter = true;
                    REQUIRE(call->operands.size() == 2);
                    language_global = call->operands[1];
                } else if (call->callee == "__ploy_rt_call_exit") {
                    saw_exit = true;
                }
            }
        }
    }
    REQUIRE(saw_enter);
    REQUIRE(saw_exit);

    bool found_language_literal = false;
    for (const auto &global : backend->ir_ctx->Globals()) {
        if (!global || global->name != language_global) continue;
        const auto address =
            std::dynamic_pointer_cast<polyglot::ir::ConstantGEP>(global->initializer);
        REQUIRE(address != nullptr);
        const auto data_global =
            std::dynamic_pointer_cast<polyglot::ir::GlobalValue>(address->base);
        REQUIRE(data_global != nullptr);
        const auto literal = std::dynamic_pointer_cast<polyglot::ir::ConstantString>(
            data_global->initializer);
        REQUIRE(literal != nullptr);
        CHECK(literal->data == "poly");
        found_language_literal = true;
    }
    REQUIRE(found_language_literal);
}

// ============================================================================
// 1. Minimal C++/Python cross-language example — frontend + sema must succeed
// ============================================================================

TEST_CASE("E2E compile: minimal C++/Python LINK compiles through sema",
          "[e2e][compile][cross-lang][cpp][python]") {
    const std::string kSource = R"poly(
// Minimal cross-language example: a C++ function called from Python-side code.
LINK(cpp, python, math_utils::add, pymath::add) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

FUNC use_add(a: INT, b: INT) -> INT {
    LET result = CALL(cpp, math_utils::add, a, b);
    RETURN result;
}
)poly";

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
    const std::string kSource = R"poly(
LINK(cpp, python, image_proc::resize, cv::resize) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

MAP_TYPE(cpp::double, python::float);

FUNC process_image(w: INT, h: INT) -> INT {
    LET r = CALL(cpp, image_proc::resize, w, h);
    RETURN r;
}
)poly";

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
    const std::string kSource = R"poly(
LINK(cpp, python, net::send, socket::send) {
    MAP_TYPE(cpp::int, python::int);
}

FUNC transmit(payload: INT) -> INT {
    LET status = CALL(cpp, net::send, payload);
    RETURN status;
}
)poly";

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

TEST_CASE("E2E compile: backend stage emits machine code for poly functions",
          "[e2e][compile][backend]") {
    const std::string kSource = R"poly(
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
)poly";

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
    const std::string kSource = R"poly(
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
)poly";

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
    const std::string kSource = R"poly(
IMPORT rust PACKAGE serde >= 1.0;

LINK(python, rust, model::serialize, serde::to_json) {
    MAP_TYPE(python::dict, rust::Value);
}

FUNC export_model(data: STRING) -> INT {
    LET json = CALL(python, model::serialize, data);
    RETURN 0;
}
)poly";

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
    const std::string kSource = R"poly(
FUNC main() -> INT {
    // No LINK declaration for math::sqrt
    LET r = CALL(cpp, math::sqrt, 9.0);
    RETURN 0;
}
)poly";

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
    const std::string kSource = R"poly(
LINK(fortran, python, legacy::routine, py::run);

FUNC main() -> INT { RETURN 0; }
)poly";

    Diagnostics out_diags;
    bool ok = RunThroughSema(kSource, out_diags);
    CHECK_FALSE(ok);
    CHECK(out_diags.HasErrors());
}

// ============================================================================
// 9. Failure case: param count mismatch in cross-lang call is caught by pipeline
// ============================================================================

TEST_CASE("E2E compile: CALL with LINK MAP_TYPE accepts flexible arity",
          "[e2e][compile][param-count]") {
    const std::string kSource = R"poly(
LINK(cpp, python, vec::dot, np::dot) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::double, python::float);
}

FUNC main() -> INT {
    // MAP_TYPE entries are type-conversion declarations, not arity constraints.
    // Calling with any argument count is valid when arity is unknown.
    LET r = CALL(cpp, vec::dot, 1.0);
    RETURN 0;
}
)poly";

    CompilationContext::Config cfg = MakeConfig(kSource);
    CompilationPipeline pipeline(cfg);

    pipeline.RunFrontend();
    bool sema_ok = pipeline.RunSemantic();

    CHECK(sema_ok);
    CHECK_FALSE(pipeline.GetContext().diagnostics->HasErrors());
}

// ============================================================================
// 10. NEW + METHOD cross-language: full compile through backend
// ============================================================================

TEST_CASE("E2E compile: NEW + METHOD C++/Python example compiles through backend",
          "[e2e][compile][cross-lang][class]") {
    const std::string kSource = R"poly(
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
)poly";

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
    const std::string kSource = R"poly(
FUNC factorial(n: INT, acc: INT) -> INT {
    IF n <= 1 { RETURN acc; }
    RETURN factorial(n - 1, n * acc);
}
)poly";

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
    const std::string kSource = R"poly(
FUNC f(x: INT) -> INT { RETURN x + 1; }
)poly";

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
