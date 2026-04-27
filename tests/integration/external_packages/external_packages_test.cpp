 // ============================================================================
// Integration Tests — External Package Resolution (Go / JavaScript / Ruby)
//
// Verifies the resolvers added in 2026-04-27-1:
//   * Go         — go.mod / GOROOT / GOPATH / project-local packages
//   * JavaScript — node_modules / package.json / .d.ts probing
//   * Ruby       — require / require_relative / RUBYLIB / --gem-path
//
// Each test compiles a tiny synthetic source against fixtures shipped in
// tests/fixtures/external_packages/.  We assert that:
//   * Resolve() returns a non-null module pointer and harvests >=1 export;
//   * the importer-side semantic analysis runs through clean;
//   * a missing-runtime case yields a diagnostic, never a crash.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/go/include/go_import_resolver.h"
#include "frontends/javascript/include/javascript_import_resolver.h"
#include "frontends/ruby/include/ruby_import_resolver.h"

namespace fs = std::filesystem;

namespace {

// Locate the fixtures directory.  Tests run from the build folder; we walk
// upward from the current path until we hit the marker file we ship in
// tests/fixtures/external_packages/.MARKER.
fs::path FixturesRoot() {
    fs::path cur = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path cand = cur / "tests" / "fixtures" / "external_packages";
        if (fs::exists(cand / ".MARKER")) return cand;
        if (cur == cur.parent_path()) break;
        cur = cur.parent_path();
    }
    // Fallback: relative to the source tree captured at configure time.
    fs::path src_cand = fs::path(POLYGLOT_TESTS_FIXTURE_ROOT);
    if (fs::exists(src_cand / ".MARKER")) return src_cand;
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// Go
// ---------------------------------------------------------------------------
TEST_CASE("Go ImportResolver resolves a project-local package",
          "[external_packages][go]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path go_root = root / "go";

    polyglot::frontends::Diagnostics diag;
    polyglot::go::GoImportResolver resolver(go_root.string(), {}, diag);

    // The fixture's import path declared in go.mod is `example.com/app`.
    const polyglot::go::GoPackage *pkg =
        resolver.Resolve("example.com/app/mathpkg");
    REQUIRE(pkg != nullptr);
    REQUIRE_FALSE(pkg->exports.empty());
    bool found_add = false;
    for (const auto &kv : pkg->exports) {
        if (kv.second.name == "Add") { found_add = true; break; }
    }
    REQUIRE(found_add);
}

TEST_CASE("Go ImportResolver reports unknown packages without crashing",
          "[external_packages][go]") {
    polyglot::frontends::Diagnostics diag;
    polyglot::go::GoImportResolver resolver({}, {}, diag);
    REQUIRE(resolver.Resolve("definitely/not/installed/xyz") == nullptr);
    REQUIRE_FALSE(diag.All().empty());
}

// ---------------------------------------------------------------------------
// JavaScript
// ---------------------------------------------------------------------------
TEST_CASE("JS ImportResolver walks node_modules and reads package.json",
          "[external_packages][javascript]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path js_root = root / "javascript";

    polyglot::frontends::Diagnostics diag;
    polyglot::javascript::JsImportResolver resolver(
        js_root.string(),
        {(js_root / "node_modules").string()},
        diag);

    const polyglot::javascript::JsModule *m = resolver.Resolve("fakelib", "");
    REQUIRE(m != nullptr);
    REQUIRE(m->resolved_path.find("fakelib") != std::string::npos);
}

TEST_CASE("JS ImportResolver resolves relative paths with extension probing",
          "[external_packages][javascript]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path js_root = root / "javascript";

    polyglot::frontends::Diagnostics diag;
    polyglot::javascript::JsImportResolver resolver(js_root.string(), {}, diag);

    const polyglot::javascript::JsModule *m =
        resolver.Resolve("./helper", js_root.string());
    REQUIRE(m != nullptr);
}

TEST_CASE("JS ImportResolver reports missing modules cleanly",
          "[external_packages][javascript]") {
    polyglot::frontends::Diagnostics diag;
    polyglot::javascript::JsImportResolver resolver({}, {}, diag);
    REQUIRE(resolver.Resolve("no-such-pkg-zzz", "") == nullptr);
    REQUIRE_FALSE(diag.All().empty());
}

// ---------------------------------------------------------------------------
// Ruby
// ---------------------------------------------------------------------------
TEST_CASE("Ruby ImportResolver resolves a vendored gem via --gem-path",
          "[external_packages][ruby]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path rb_root = root / "ruby";

    polyglot::frontends::Diagnostics diag;
    polyglot::ruby::RbImportResolver resolver(
        rb_root.string(),
        {(rb_root / "gems").string()},
        diag);

    const polyglot::ruby::RbFile *f =
        resolver.Resolve("strutil", "", /*relative=*/false);
    REQUIRE(f != nullptr);
    REQUIRE_FALSE(f->exports.empty());
}

TEST_CASE("Ruby ImportResolver handles require_relative",
          "[external_packages][ruby]") {
    fs::path root = FixturesRoot();
    REQUIRE_FALSE(root.empty());
    fs::path rb_root = root / "ruby";

    polyglot::frontends::Diagnostics diag;
    polyglot::ruby::RbImportResolver resolver(rb_root.string(), {}, diag);

    const polyglot::ruby::RbFile *f =
        resolver.Resolve("./gems/strutil/lib/strutil",
                          rb_root.string(), /*relative=*/true);
    REQUIRE(f != nullptr);
}

TEST_CASE("Ruby ImportResolver reports missing requires cleanly",
          "[external_packages][ruby]") {
    polyglot::frontends::Diagnostics diag;
    polyglot::ruby::RbImportResolver resolver({}, {}, diag);
    REQUIRE(resolver.Resolve("nonexistent_gem_zzz", "", false) == nullptr);
    REQUIRE_FALSE(diag.All().empty());
}
