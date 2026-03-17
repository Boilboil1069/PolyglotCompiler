// ============================================================================
// Unit tests for FrontendRegistry — unified language frontend dispatch
//
// Covers:
//   1. Auto-registration of all built-in frontends
//   2. Lookup by canonical name
//   3. Lookup by alias (e.g. "c" -> cpp, "csharp" -> dotnet)
//   4. Lookup by file extension
//   5. Language detection from file path
//   6. SupportedLanguages enumeration
//   7. AllFrontends enumeration
//   8. Unknown language returns nullptr
//   9. Clear and re-register
//  10. ILanguageFrontend interface contract (Tokenize / Analyze / Lower)
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/language_frontend.h"

// Include adapter headers to ensure static auto-registration fires.
#include "frontends/ploy/include/ploy_frontend.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/python/include/python_frontend.h"
#include "frontends/rust/include/rust_frontend.h"
#include "frontends/java/include/java_frontend.h"
#include "frontends/dotnet/include/dotnet_frontend.h"

using polyglot::frontends::FrontendRegistry;
using polyglot::frontends::ILanguageFrontend;

// ============================================================================
// Helper: check that a language name exists in a sorted list
// ============================================================================

static bool Contains(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// ============================================================================
// Section: Auto-registration
// ============================================================================

TEST_CASE("[registry] All built-in frontends are registered", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();
    auto langs = reg.SupportedLanguages();

    REQUIRE(langs.size() >= 6);
    CHECK(Contains(langs, "ploy"));
    CHECK(Contains(langs, "cpp"));
    CHECK(Contains(langs, "python"));
    CHECK(Contains(langs, "rust"));
    CHECK(Contains(langs, "java"));
    CHECK(Contains(langs, "dotnet"));
}

// ============================================================================
// Section: Lookup by canonical name
// ============================================================================

TEST_CASE("[registry] Lookup by canonical name", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();

    SECTION("ploy") {
        auto *fe = reg.GetFrontend("ploy");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "ploy");
        CHECK(fe->DisplayName() == "Ploy");
    }

    SECTION("cpp") {
        auto *fe = reg.GetFrontend("cpp");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "cpp");
        CHECK(fe->DisplayName() == "C++");
    }

    SECTION("python") {
        auto *fe = reg.GetFrontend("python");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "python");
        CHECK(fe->DisplayName() == "Python");
    }

    SECTION("rust") {
        auto *fe = reg.GetFrontend("rust");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "rust");
        CHECK(fe->DisplayName() == "Rust");
    }

    SECTION("java") {
        auto *fe = reg.GetFrontend("java");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "java");
        CHECK(fe->DisplayName() == "Java");
    }

    SECTION("dotnet") {
        auto *fe = reg.GetFrontend("dotnet");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "dotnet");
    }
}

// ============================================================================
// Section: Lookup by alias
// ============================================================================

TEST_CASE("[registry] Lookup by alias", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();

    SECTION("'c' resolves to cpp frontend") {
        auto *fe = reg.GetFrontend("c");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "cpp");
    }

    SECTION("'c++' resolves to cpp frontend") {
        auto *fe = reg.GetFrontend("c++");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "cpp");
    }

    SECTION("'csharp' resolves to dotnet frontend") {
        auto *fe = reg.GetFrontend("csharp");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "dotnet");
    }

    SECTION("case-insensitive alias lookup") {
        auto *fe = reg.GetFrontend("Python");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "python");
    }
}

// ============================================================================
// Section: Lookup by extension
// ============================================================================

TEST_CASE("[registry] Lookup by file extension", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();

    SECTION(".ploy -> ploy") {
        auto *fe = reg.GetFrontendByExtension(".ploy");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "ploy");
    }

    SECTION(".cpp -> cpp") {
        auto *fe = reg.GetFrontendByExtension(".cpp");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "cpp");
    }

    SECTION(".cc -> cpp") {
        auto *fe = reg.GetFrontendByExtension(".cc");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "cpp");
    }

    SECTION(".py -> python") {
        auto *fe = reg.GetFrontendByExtension(".py");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "python");
    }

    SECTION(".rs -> rust") {
        auto *fe = reg.GetFrontendByExtension(".rs");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "rust");
    }

    SECTION(".java -> java") {
        auto *fe = reg.GetFrontendByExtension(".java");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "java");
    }

    SECTION(".cs -> dotnet") {
        auto *fe = reg.GetFrontendByExtension(".cs");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "dotnet");
    }

    SECTION("case-insensitive extension") {
        auto *fe = reg.GetFrontendByExtension(".PY");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "python");
    }

    SECTION("unknown extension returns nullptr") {
        auto *fe = reg.GetFrontendByExtension(".xyz");
        CHECK(fe == nullptr);
    }
}

// ============================================================================
// Section: Language detection from file path
// ============================================================================

TEST_CASE("[registry] DetectLanguage from file path", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();

    CHECK(reg.DetectLanguage("main.ploy") == "ploy");
    CHECK(reg.DetectLanguage("src/app.cpp") == "cpp");
    CHECK(reg.DetectLanguage("lib/utils.py") == "python");
    CHECK(reg.DetectLanguage("mod.rs") == "rust");
    CHECK(reg.DetectLanguage("Main.java") == "java");
    CHECK(reg.DetectLanguage("Program.cs") == "dotnet");
    CHECK(reg.DetectLanguage("README.md").empty());
    CHECK(reg.DetectLanguage("Makefile").empty());
}

// ============================================================================
// Section: Enumeration
// ============================================================================

TEST_CASE("[registry] AllFrontends returns all registered frontends", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();
    auto all = reg.AllFrontends();
    REQUIRE(all.size() >= 6);

    // Every frontend pointer must be non-null and have a non-empty name
    for (const auto *fe : all) {
        REQUIRE(fe != nullptr);
        CHECK(!fe->Name().empty());
        CHECK(!fe->DisplayName().empty());
        CHECK(!fe->Extensions().empty());
    }
}

// ============================================================================
// Section: Unknown language
// ============================================================================

TEST_CASE("[registry] Unknown language returns nullptr", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();
    CHECK(reg.GetFrontend("fortran") == nullptr);
    CHECK(reg.GetFrontend("") == nullptr);
    CHECK(reg.GetFrontend("cobol") == nullptr);
}

// ============================================================================
// Section: NeedsPreprocessing contract
// ============================================================================

TEST_CASE("[registry] NeedsPreprocessing reflects frontend capabilities", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();

    // C++ requires preprocessing
    auto *cpp_fe = reg.GetFrontend("cpp");
    REQUIRE(cpp_fe != nullptr);
    CHECK(cpp_fe->NeedsPreprocessing() == true);

    // Python does not require preprocessing
    auto *py_fe = reg.GetFrontend("python");
    REQUIRE(py_fe != nullptr);
    CHECK(py_fe->NeedsPreprocessing() == false);

    // Ploy does not require preprocessing
    auto *ploy_fe = reg.GetFrontend("ploy");
    REQUIRE(ploy_fe != nullptr);
    CHECK(ploy_fe->NeedsPreprocessing() == false);
}

// ============================================================================
// Section: Tokenize contract
// ============================================================================

TEST_CASE("[registry] Tokenize produces tokens for valid source", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();

    SECTION("ploy tokenization") {
        auto *fe = reg.GetFrontend("ploy");
        REQUIRE(fe != nullptr);
        auto tokens = fe->Tokenize("LET x = 42;", "test.ploy");
        CHECK(!tokens.empty());
    }

    SECTION("cpp tokenization") {
        auto *fe = reg.GetFrontend("cpp");
        REQUIRE(fe != nullptr);
        auto tokens = fe->Tokenize("int main() { return 0; }", "test.cpp");
        CHECK(!tokens.empty());
    }

    SECTION("python tokenization") {
        auto *fe = reg.GetFrontend("python");
        REQUIRE(fe != nullptr);
        auto tokens = fe->Tokenize("x = 42", "test.py");
        CHECK(!tokens.empty());
    }

    SECTION("rust tokenization") {
        auto *fe = reg.GetFrontend("rust");
        REQUIRE(fe != nullptr);
        auto tokens = fe->Tokenize("fn main() {}", "test.rs");
        CHECK(!tokens.empty());
    }

    SECTION("java tokenization") {
        auto *fe = reg.GetFrontend("java");
        REQUIRE(fe != nullptr);
        auto tokens = fe->Tokenize("class Main {}", "Test.java");
        CHECK(!tokens.empty());
    }

    SECTION("dotnet tokenization") {
        auto *fe = reg.GetFrontend("dotnet");
        REQUIRE(fe != nullptr);
        auto tokens = fe->Tokenize("class Program {}", "Program.cs");
        CHECK(!tokens.empty());
    }
}

// ============================================================================
// Section: Analyze contract
// ============================================================================

TEST_CASE("[registry] Analyze reports diagnostics for invalid source", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();
    polyglot::frontends::Diagnostics diags;
    polyglot::frontends::FrontendOptions opts;

    SECTION("ploy analysis on valid source succeeds") {
        auto *fe = reg.GetFrontend("ploy");
        REQUIRE(fe != nullptr);
        bool ok = fe->Analyze("LET x = 42;", "test.ploy", diags, opts);
        CHECK(ok);
    }

    SECTION("cpp analysis on valid source succeeds") {
        auto *fe = reg.GetFrontend("cpp");
        REQUIRE(fe != nullptr);
        bool ok = fe->Analyze("int main() { return 0; }", "test.cpp", diags, opts);
        CHECK(ok);
    }
}

// ============================================================================
// Section: Clear and re-register
// ============================================================================

TEST_CASE("[registry] Clear removes all frontends", "[frontend_registry]") {
    // Create a local registry-like test by using the global one carefully.
    // We save the state, clear, verify empty, then re-register.
    auto &reg = FrontendRegistry::Instance();

    // Snapshot current state
    auto original_langs = reg.SupportedLanguages();
    REQUIRE(!original_langs.empty());

    // Clear
    reg.Clear();
    CHECK(reg.SupportedLanguages().empty());
    CHECK(reg.GetFrontend("ploy") == nullptr);
    CHECK(reg.GetFrontend("cpp") == nullptr);

    // Re-register all frontends
    reg.Register(std::make_shared<polyglot::ploy::PloyLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::cpp::CppLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::python::PythonLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::rust::RustLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::java::JavaLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::dotnet::DotnetLanguageFrontend>());

    // Verify restoration
    auto restored = reg.SupportedLanguages();
    CHECK(restored.size() == original_langs.size());
    CHECK(reg.GetFrontend("ploy") != nullptr);
    CHECK(reg.GetFrontend("cpp") != nullptr);
}

// ============================================================================
// Section: Custom frontend registration
// ============================================================================

namespace {

class MockFrontend : public polyglot::frontends::ILanguageFrontend {
  public:
    std::string Name() const override { return "mock"; }
    std::string DisplayName() const override { return "Mock Language"; }

    std::vector<std::string> Extensions() const override { return {".mock"}; }
    std::vector<std::string> Aliases() const override { return {"mk", "mck"}; }

    std::vector<polyglot::frontends::Token> Tokenize(
        const std::string & /*source*/,
        const std::string & /*filename*/) const override {
        return {};
    }

    bool Analyze(
        const std::string & /*source*/,
        const std::string & /*filename*/,
        polyglot::frontends::Diagnostics & /*diagnostics*/,
        const polyglot::frontends::FrontendOptions & /*options*/) const override {
        return true;
    }

    polyglot::frontends::FrontendResult Lower(
        const std::string & /*source*/,
        const std::string & /*filename*/,
        polyglot::ir::IRContext & /*ir_ctx*/,
        polyglot::frontends::Diagnostics & /*diagnostics*/,
        const polyglot::frontends::FrontendOptions & /*options*/) const override {
        polyglot::frontends::FrontendResult r;
        r.success = true;
        r.lowered = true;
        return r;
    }
};

}  // anonymous namespace

TEST_CASE("[registry] Custom frontend can be registered and looked up", "[frontend_registry]") {
    auto &reg = FrontendRegistry::Instance();

    // Register mock frontend
    auto mock = std::make_shared<MockFrontend>();
    reg.Register(mock);

    SECTION("lookup by name") {
        auto *fe = reg.GetFrontend("mock");
        REQUIRE(fe != nullptr);
        CHECK(fe->DisplayName() == "Mock Language");
    }

    SECTION("lookup by alias") {
        auto *fe = reg.GetFrontend("mk");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "mock");

        auto *fe2 = reg.GetFrontend("mck");
        REQUIRE(fe2 != nullptr);
        CHECK(fe2->Name() == "mock");
    }

    SECTION("lookup by extension") {
        auto *fe = reg.GetFrontendByExtension(".mock");
        REQUIRE(fe != nullptr);
        CHECK(fe->Name() == "mock");
    }

    SECTION("detect language from path") {
        auto lang = reg.DetectLanguage("test_file.mock");
        CHECK(lang == "mock");
    }

    SECTION("appears in SupportedLanguages") {
        auto langs = reg.SupportedLanguages();
        CHECK(Contains(langs, "mock"));
    }

    SECTION("appears in AllFrontends") {
        auto all = reg.AllFrontends();
        bool found = false;
        for (auto *fe : all) {
            if (fe->Name() == "mock") { found = true; break; }
        }
        CHECK(found);
    }
}
