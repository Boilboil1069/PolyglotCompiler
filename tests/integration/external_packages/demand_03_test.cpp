// ============================================================================
// Integration Tests — External Package Resolution (demand 2026-04-26-03)
//
// Hermetic end-to-end coverage of the loader-driven external-package paths
// added by demand 03.  No network access; no system-installed runtimes are
// required.  Every artefact lives under tests/fixtures/external_packages/.
//
// Coverage:
//   * C++         — preprocessor #include resolution through -I search path.
//   * Python      — PyiLoader resolves a fake `numlib` package via --python-stubs.
//   * Rust        — CrateLoader resolves a fake `miniutils` crate via --extern.
//
// (Java .class/.jar, .NET .dll and Ploy IMPORT PACKAGE happy-paths are
// already exercised by the dedicated unit-test suites under
// tests/unit/frontends/{java,dotnet,ploy}/.)
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/preprocessor.h"
#include "frontends/python/include/pyi_loader.h"
#include "frontends/rust/include/crate_loader.h"

namespace fs = std::filesystem;

namespace {

// Locate the fixtures root.  Mirrors the helper from
// external_packages_test.cpp so the two suites can share fixture trees.
fs::path FixturesRoot() {
    fs::path cur = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path cand = cur / "tests" / "fixtures" / "external_packages";
        if (fs::exists(cand / ".MARKER_03")) return cand;
        if (cur == cur.parent_path()) break;
        cur = cur.parent_path();
    }
#ifdef POLYGLOT_TESTS_FIXTURE_ROOT
    fs::path src_cand = fs::path(POLYGLOT_TESTS_FIXTURE_ROOT);
    if (fs::exists(src_cand / ".MARKER_03")) return src_cand;
#endif
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// C++ — preprocessor #include via -I search path
// ---------------------------------------------------------------------------
TEST_CASE("Demand 03 / C++ preprocessor expands #include from -I path",
          "[external_packages][demand03][cpp]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path cpp_include = root / "cpp" / "include";
    REQUIRE(fs::exists(cpp_include / "mathlib.h"));

    polyglot::frontends::Diagnostics diags;
    polyglot::frontends::Preprocessor pp(diags);
    pp.AddIncludePath(cpp_include.string());

    const std::string src = R"cpp(
#include "mathlib.h"

int driver(int x) {
    return mathlib_add(x, MATHLIB_VERSION_MAJOR);
}
)cpp";

    std::string processed = pp.Process(src, "<demand03_cpp>");

    // The stub declarations from the header must appear in the expanded
    // translation unit, proving the -I path was honoured and the directive
    // wasn't silently skipped.
    CHECK(processed.find("int mathlib_add(int a, int b)") != std::string::npos);
    CHECK(processed.find("int mathlib_mul(int a, int b)") != std::string::npos);

    // Object-like macro must have been substituted in the user code.
    CHECK(processed.find("MATHLIB_VERSION_MAJOR") == std::string::npos);
    CHECK(processed.find("4") != std::string::npos);

    // No diagnostics produced for a well-formed include.
    CHECK_FALSE(diags.HasErrors());
}

TEST_CASE("Demand 03 / C++ preprocessor reports missing header diagnostically",
          "[external_packages][demand03][cpp][diagnostics]") {
    polyglot::frontends::Diagnostics diags;
    polyglot::frontends::Preprocessor pp(diags);
    // Intentionally do NOT add any -I path.

    const std::string src = "#include \"this_header_does_not_exist.h\"\n";
    (void)pp.Process(src, "<demand03_cpp_missing>");

    // Missing-include must surface as a diagnostic, not a crash and not a
    // silent skip.  The exact code text is implementation-defined but the
    // message stream should be non-empty.
    CHECK(diags.HasErrors());
}

// ---------------------------------------------------------------------------
// Python — .pyi stub resolution via --python-stubs path
// ---------------------------------------------------------------------------
TEST_CASE("Demand 03 / Python PyiLoader resolves a fake numlib stub",
          "[external_packages][demand03][python]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path stubs_dir = root / "python" / "stubs";
    REQUIRE(fs::exists(stubs_dir / "numlib.pyi"));

    polyglot::frontends::Diagnostics diags;
    polyglot::python::PyiLoader loader({stubs_dir.string()}, diags);

    const auto *mod = loader.Resolve("numlib");
    REQUIRE(mod != nullptr);

    // Top-level function exports must be visible.
    CHECK(mod->exports.count("array")  == 1);
    CHECK(mod->exports.count("mean")   == 1);
    CHECK(mod->exports.count("sum")    == 1);
    // Module-level constants and classes too.
    CHECK(mod->exports.count("PI")     == 1);
    CHECK(mod->exports.count("Vector") == 1);

    CHECK_FALSE(diags.HasErrors());
}

TEST_CASE("Demand 03 / Python PyiLoader returns nullptr for unknown module",
          "[external_packages][demand03][python][diagnostics]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path stubs_dir = root / "python" / "stubs";

    polyglot::frontends::Diagnostics diags;
    polyglot::python::PyiLoader loader({stubs_dir.string()}, diags);

    // No stub for "definitely_not_a_real_module" exists in the fixture.
    const auto *mod = loader.Resolve("definitely_not_a_real_module");
    CHECK(mod == nullptr);
    // A missing user-requested stub is reported as a non-fatal warning
    // (HasErrors() may stay false); we only require no crash + null result.
}

// ---------------------------------------------------------------------------
// Rust — CrateLoader resolves a source crate via --extern
// ---------------------------------------------------------------------------
TEST_CASE("Demand 03 / Rust CrateLoader indexes a fake source crate via --extern",
          "[external_packages][demand03][rust]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path crate_dir = root / "rust" / "miniutils";
    REQUIRE(fs::exists(crate_dir / "Cargo.toml"));
    REQUIRE(fs::exists(crate_dir / "src" / "lib.rs"));

    polyglot::frontends::Diagnostics diags;
    std::vector<std::pair<std::string, std::string>> externs = {
        {"miniutils", crate_dir.string()},
    };

    polyglot::rust::CrateLoader loader(/*crate_dir=*/"", externs, diags);

    const auto *crate = loader.ResolveCrate("miniutils");
    REQUIRE(crate != nullptr);
    CHECK(crate->name == "miniutils");
    // Source crate (not .rlib/.rmeta), so items must be populated.
    CHECK_FALSE(crate->is_binary_artifact);
    CHECK_FALSE(crate->items.empty());

    // Public functions defined in src/lib.rs must round-trip through the
    // qualified-path index.
    const auto *dbl   = loader.ResolvePath("miniutils::double");
    const auto *clmp  = loader.ResolvePath("miniutils::clamp");
    REQUIRE(dbl  != nullptr);
    REQUIRE(clmp != nullptr);
    CHECK(dbl->kind  == polyglot::rust::CrateItemKind::kFunction);
    CHECK(clmp->kind == polyglot::rust::CrateItemKind::kFunction);

    // Public const must be visible too (kConst or kStatic depending on parser).
    const auto *ver = loader.ResolvePath("miniutils::VERSION");
    REQUIRE(ver != nullptr);
}

TEST_CASE("Demand 03 / Rust CrateLoader exposes nested module items",
          "[external_packages][demand03][rust][nested]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path crate_dir = root / "rust" / "miniutils";

    polyglot::frontends::Diagnostics diags;
    std::vector<std::pair<std::string, std::string>> externs = {
        {"miniutils", crate_dir.string()},
    };
    polyglot::rust::CrateLoader loader("", externs, diags);

    // `pub mod inner { pub fn triple(...) }` — the nested function must be
    // reachable through its qualified path.
    const auto *triple = loader.ResolvePath("miniutils::inner::triple");
    REQUIRE(triple != nullptr);
    CHECK(triple->kind == polyglot::rust::CrateItemKind::kFunction);
}
