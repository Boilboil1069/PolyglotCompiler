// ============================================================================
// Integration Tests — Per-language version gating and ploy LANG propagation
//
// This suite validates the language-version pinning machinery introduced
// for cross-language compilation:
//
//   * Each native frontend (C++, Python, Java, .NET, Rust, JavaScript)
//     accepts a feature-positive fixture under a sufficiently new
//     dialect / release / edition / ECMA version, and rejects the same
//     source with `kLangVersionMismatch` under an older one.
//   * The Go and Ruby frontends do not yet implement parser-level gating
//     for the demand-prescribed features (generics, pattern matching),
//     so for those languages we instead assert that the .ploy `LANG`
//     pragma propagates a version pin into every cross-language call
//     directed at that language.
//   * Per-call-site `@LANG` annotations on the same .ploy module produce
//     two distinct `lang_version_pin` values for two LINKs to the same
//     target language — proving the dual-ABI bridge code path is
//     reachable.
//
// All test data is embedded inline so the suite is hermetic and does not
// depend on the on-disk fixtures (kept under
// tests/integration/language_versions/<lang>/ for documentation and
// future native-toolchain probes).
// ============================================================================

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_versions.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/dotnet/include/dotnet_lexer.h"
#include "frontends/dotnet/include/dotnet_parser.h"
#include "frontends/java/include/java_lexer.h"
#include "frontends/java/include/java_parser.h"
#include "frontends/javascript/include/javascript_lexer.h"
#include "frontends/javascript/include/javascript_parser.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::ErrorCode;

namespace {

// Count diagnostics whose code is `kLangVersionMismatch`.
size_t CountVersionMismatches(const Diagnostics &diags) {
    size_t n = 0;
    for (const auto &d : diags.All()) {
        if (d.code == ErrorCode::kLangVersionMismatch)
            ++n;
    }
    return n;
}

// Walk a ploy module and return every cross-language call expression.
void CollectCallsInExpression(const std::shared_ptr<polyglot::ploy::Expression> &expr,
                              std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>>
                                  &out) {
    if (!expr) return;
    if (auto call =
            std::dynamic_pointer_cast<polyglot::ploy::CrossLangCallExpression>(expr)) {
        out.push_back(call);
    }
}

void CollectCallsInStatement(const std::shared_ptr<polyglot::ploy::Statement> &stmt,
                             std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>>
                                 &out) {
    if (!stmt) return;
    if (auto es = std::dynamic_pointer_cast<polyglot::ploy::ExprStatement>(stmt)) {
        CollectCallsInExpression(es->expr, out);
        return;
    }
    if (auto with_lang = std::dynamic_pointer_cast<polyglot::ploy::WithLangBlock>(stmt)) {
        for (const auto &s : with_lang->body) CollectCallsInStatement(s, out);
        return;
    }
    if (auto anno = std::dynamic_pointer_cast<polyglot::ploy::LangAnnotation>(stmt)) {
        CollectCallsInStatement(anno->target, out);
        return;
    }
    if (auto let = std::dynamic_pointer_cast<polyglot::ploy::VarDecl>(stmt)) {
        CollectCallsInExpression(let->init, out);
        return;
    }
    if (auto fn = std::dynamic_pointer_cast<polyglot::ploy::FuncDecl>(stmt)) {
        for (const auto &s : fn->body) CollectCallsInStatement(s, out);
        return;
    }
}

void CollectCalls(const std::shared_ptr<polyglot::ploy::Module> &mod,
                  std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>> &out) {
    if (!mod) return;
    for (const auto &s : mod->declarations) CollectCallsInStatement(s, out);
}

// Find every LINK declaration in a module so the dual-pin test can also
// assert that two LINK targets to the same language coexist.
std::vector<std::shared_ptr<polyglot::ploy::LinkDecl>> CollectLinks(
    const std::shared_ptr<polyglot::ploy::Module> &mod) {
    std::vector<std::shared_ptr<polyglot::ploy::LinkDecl>> out;
    if (!mod) return out;
    for (const auto &s : mod->declarations) {
        if (auto link = std::dynamic_pointer_cast<polyglot::ploy::LinkDecl>(s)) {
            out.push_back(link);
            continue;
        }
        // `@LANG (...) LINK ...` — the annotation wraps the LinkDecl so we
        // must unwrap it to find the underlying directive.
        if (auto anno =
                std::dynamic_pointer_cast<polyglot::ploy::LangAnnotation>(s)) {
            if (auto link =
                    std::dynamic_pointer_cast<polyglot::ploy::LinkDecl>(anno->target))
                out.push_back(link);
        }
    }
    return out;
}

} // namespace

// ============================================================================
// C++ — concept / requires gated on c++20+
// ============================================================================

TEST_CASE("Lang versions / C++: 'concept' is accepted on c++20", "[lang-versions][cpp]") {
    const char *src = R"cpp(
template <typename T>
concept Integral = true;

template <typename T> requires Integral<T>
T add(T a, T b) { return a + b; }
)cpp";

    Diagnostics diags;
    polyglot::cpp::CppLexer lex(src, "<cpp20>");
    polyglot::cpp::CppParser parser(lex, diags);
    parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp20);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) == 0);
}

TEST_CASE("Lang versions / C++: 'concept' is rejected on c++17", "[lang-versions][cpp]") {
    const char *src = R"cpp(
template <typename T>
concept Integral = true;
)cpp";

    Diagnostics diags;
    polyglot::cpp::CppLexer lex(src, "<cpp17>");
    polyglot::cpp::CppParser parser(lex, diags);
    parser.SetCppDialect(polyglot::frontends::CppDialect::kCpp17);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) >= 1);
}

// ============================================================================
// Python — walrus on 3.8+
// ============================================================================

TEST_CASE("Lang versions / Python: walrus is accepted on 3.8", "[lang-versions][python]") {
    const char *src = "if (n := 10) > 5:\n    pass\n";
    Diagnostics diags;
    polyglot::python::PythonLexer lex(src, "<py38>", &diags);
    polyglot::python::PythonParser parser(lex, diags);
    parser.SetPythonVersion(polyglot::frontends::PythonVersion::kPy3_8);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) == 0);
}

TEST_CASE("Lang versions / Python: walrus is rejected on 3.6", "[lang-versions][python]") {
    const char *src = "if (n := 10) > 5:\n    pass\n";
    Diagnostics diags;
    polyglot::python::PythonLexer lex(src, "<py36>", &diags);
    polyglot::python::PythonParser parser(lex, diags);
    parser.SetPythonVersion(polyglot::frontends::PythonVersion::kPy3_6);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) >= 1);
}

// ============================================================================
// Java — record on 17+
// ============================================================================

TEST_CASE("Lang versions / Java: record is accepted on 17", "[lang-versions][java]") {
    const char *src = R"java(
public record Point(int x, int y) { }
)java";

    Diagnostics diags;
    polyglot::java::JavaLexer lex(src, "<java17>");
    polyglot::java::JavaParser parser(lex, diags);
    parser.SetJavaRelease(polyglot::frontends::JavaRelease::kJava17);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) == 0);
}

TEST_CASE("Lang versions / Java: record is rejected on 8", "[lang-versions][java]") {
    // Top-level record so the parser hits the Java-17 gate.  When a record
    // is nested inside a class body the gate currently lives one frame
    // deeper, so we deliberately keep the construct top-level for the
    // version-mismatch path.
    const char *src = R"java(
public record Point(int x, int y) { }
)java";

    Diagnostics diags;
    polyglot::java::JavaLexer lex(src, "<java8>");
    polyglot::java::JavaParser parser(lex, diags);
    parser.SetJavaRelease(polyglot::frontends::JavaRelease::kJava8);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) >= 1);
}

// ============================================================================
// .NET / C# — file-scoped types on Cs11+
// ============================================================================

TEST_CASE("Lang versions / .NET: 'file class' is accepted on Cs11", "[lang-versions][dotnet]") {
    const char *src = R"cs(
namespace Demo;
file class Helper { }
)cs";

    Diagnostics diags;
    polyglot::dotnet::DotnetLexer lex(src, "<cs11>");
    polyglot::dotnet::DotnetParser parser(lex, diags);
    parser.SetDotnetLangVersion(polyglot::frontends::DotnetLangVersion::kCs11);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) == 0);
}

TEST_CASE("Lang versions / .NET: 'file class' is rejected on Cs9", "[lang-versions][dotnet]") {
    const char *src = R"cs(
namespace Demo;
file class Helper { }
)cs";

    Diagnostics diags;
    polyglot::dotnet::DotnetLexer lex(src, "<cs9>");
    polyglot::dotnet::DotnetParser parser(lex, diags);
    parser.SetDotnetLangVersion(polyglot::frontends::DotnetLangVersion::kCs9);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) >= 1);
}

// ============================================================================
// Rust — let-else on edition 2021+
// ============================================================================

TEST_CASE("Lang versions / Rust: let-else is accepted on edition 2021",
          "[lang-versions][rust]") {
    const char *src = R"rust(
pub fn first(values: &[i32]) -> i32 {
    let Some(v) = values.first() else { return 0; };
    *v
}
)rust";

    Diagnostics diags;
    polyglot::rust::RustLexer lex(src, "<rust2021>");
    polyglot::rust::RustParser parser(lex, diags);
    parser.SetRustEdition(polyglot::frontends::RustEdition::kE2021);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) == 0);
}

TEST_CASE("Lang versions / Rust: let-else is rejected on edition 2018",
          "[lang-versions][rust]") {
    const char *src = R"rust(
pub fn first(values: &[i32]) -> i32 {
    let Some(v) = values.first() else { return 0; };
    *v
}
)rust";

    Diagnostics diags;
    polyglot::rust::RustLexer lex(src, "<rust2018>");
    polyglot::rust::RustParser parser(lex, diags);
    parser.SetRustEdition(polyglot::frontends::RustEdition::kE2018);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) >= 1);
}

// ============================================================================
// JavaScript — optional chaining on Es2020+
// ============================================================================

TEST_CASE("Lang versions / JS: optional chaining is accepted on ES2020",
          "[lang-versions][javascript]") {
    const char *src = "function f(u) { return u?.address?.city; }\n";
    Diagnostics diags;
    polyglot::javascript::JsLexer lex(src, "<es2020>");
    polyglot::javascript::JsParser parser(lex, diags);
    parser.SetEcmaVersion(polyglot::frontends::EcmaVersion::kEs2020);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) == 0);
}

TEST_CASE("Lang versions / JS: optional chaining is rejected on ES2017",
          "[lang-versions][javascript]") {
    const char *src = "function f(u) { return u?.address?.city; }\n";
    Diagnostics diags;
    polyglot::javascript::JsLexer lex(src, "<es2017>");
    polyglot::javascript::JsParser parser(lex, diags);
    parser.SetEcmaVersion(polyglot::frontends::EcmaVersion::kEs2017);
    parser.ParseModule();
    CHECK(CountVersionMismatches(diags) >= 1);
}

// ============================================================================
// Go / Ruby — propagation through .ploy LANG pragma
// (Go generics and Ruby `case ... in` are not parser-gated yet; the pin
// is still required to flow through to every cross-language call so the
// runtime / linker stage can dispatch to a matching toolchain.)
// ============================================================================

namespace {

std::shared_ptr<polyglot::ploy::Module> ParsePloy(const std::string &code, Diagnostics &diags) {
    polyglot::ploy::PloyLexer lexer(code, "<ploy>");
    polyglot::ploy::PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto mod = parser.TakeModule();
    if (mod) {
        polyglot::ploy::PloySema sema(diags, polyglot::ploy::PloySemaOptions{});
        sema.Analyze(mod);
    }
    return mod;
}

} // namespace

TEST_CASE("Lang versions / Go: LANG pragma propagates 1.18 onto every call",
          "[lang-versions][go]") {
    const std::string code = R"PLOY(
LANG go = "1.18";

LINK(go, ploy, slices::map, host_slices_map);
LINK(go, ploy, slices::filter, host_slices_filter);

CALL(go, slices::map, 1);
CALL(go, slices::filter, 2);
)PLOY";
    Diagnostics diags;
    auto mod = ParsePloy(code, diags);
    REQUIRE(mod != nullptr);
    REQUIRE(!diags.HasErrors());

    std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>> calls;
    CollectCalls(mod, calls);
    REQUIRE(calls.size() == 2);
    for (const auto &c : calls) {
        CHECK(c->language == "go");
        CHECK(c->lang_version_pin == "1.18");
    }
}

TEST_CASE("Lang versions / Go: missing pragma leaves pin empty", "[lang-versions][go]") {
    const std::string code = "CALL(go, slices::map, 1);\n";
    Diagnostics diags;
    auto mod = ParsePloy(code, diags);
    REQUIRE(mod != nullptr);

    std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>> calls;
    CollectCalls(mod, calls);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]->lang_version_pin.empty());
}

TEST_CASE("Lang versions / Ruby: LANG pragma propagates 3.0 onto every call",
          "[lang-versions][ruby]") {
    const std::string code = R"PLOY(
LANG ruby = "3.0";

LINK(ruby, ploy, kernel::puts, host_puts);
LINK(ruby, ploy, kernel::raise, host_raise);

CALL(ruby, kernel::puts, "hi");
CALL(ruby, kernel::raise, "boom");
)PLOY";
    Diagnostics diags;
    auto mod = ParsePloy(code, diags);
    REQUIRE(mod != nullptr);
    REQUIRE(!diags.HasErrors());

    std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>> calls;
    CollectCalls(mod, calls);
    REQUIRE(calls.size() == 2);
    for (const auto &c : calls) {
        CHECK(c->language == "ruby");
        CHECK(c->lang_version_pin == "3.0");
    }
}

TEST_CASE("Lang versions / Ruby: missing pragma leaves pin empty",
          "[lang-versions][ruby]") {
    const std::string code = "CALL(ruby, kernel::puts, 1);\n";
    Diagnostics diags;
    auto mod = ParsePloy(code, diags);
    REQUIRE(mod != nullptr);

    std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>> calls;
    CollectCalls(mod, calls);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]->lang_version_pin.empty());
}

// ============================================================================
// Per-callsite version coexistence
// ============================================================================

TEST_CASE("Lang versions / ploy: per-callsite @LANG produces dual ABI pins",
          "[lang-versions][ploy][per-callsite]") {
    const std::string code = R"PLOY(
LANG cpp = "c++17";

LINK(cpp, ploy, math17::add, host::add17);
@LANG (cpp="c++23")
LINK(cpp, ploy, math23::add, host::add23);

FUNC mix(a: INT, b: INT) -> INT {
    LET r17 = CALL(cpp, math17::add, a, b);
    @LANG (cpp="c++23")
    LET r23 = CALL(cpp, math23::add, a, b);
    RETURN r17 + r23;
}
)PLOY";

    Diagnostics diags;
    auto mod = ParsePloy(code, diags);
    REQUIRE(mod != nullptr);
    REQUIRE(!diags.HasErrors());

    std::vector<std::shared_ptr<polyglot::ploy::CrossLangCallExpression>> calls;
    CollectCalls(mod, calls);
    REQUIRE(calls.size() == 2);

    // Two distinct pins must coexist within the same module — that is
    // precisely the dual-ABI bridge code path the linker must support.
    std::vector<std::string> pins;
    pins.reserve(calls.size());
    for (const auto &c : calls) pins.push_back(c->lang_version_pin);
    std::sort(pins.begin(), pins.end());
    REQUIRE(pins.size() == 2);
    CHECK(pins[0] == "c++17");
    CHECK(pins[1] == "c++23");

    auto links = CollectLinks(mod);
    REQUIRE(links.size() == 2);
    // The two LINKs target distinct C++ symbols even though they share the
    // same target language: this is precisely the dual-ABI bridge layout
    // the linker must emit.
    std::vector<std::string> link_targets;
    for (const auto &l : links) link_targets.push_back(l->target_symbol);
    std::sort(link_targets.begin(), link_targets.end());
    CHECK(link_targets[0] == "math17::add");
    CHECK(link_targets[1] == "math23::add");
}
