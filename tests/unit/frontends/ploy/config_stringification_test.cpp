// ============================================================================
// config_stringification_test.cpp
//
// Exercises the stringified `CONFIG <language> "<package_manager>"
// "<path>";` form (since v1.12.0) and the legacy keyword-driven form's
// deprecation diagnostic.
// ============================================================================

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_config_registry.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::AllConfigManagerEntries;
using polyglot::ploy::LegacyConfigKeywordToManagerName;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::ResolveConfigManager;
using polyglot::ploy::VenvConfigDecl;

namespace {

struct AnalyzeResult {
  Diagnostics diags;
  std::shared_ptr<polyglot::ploy::Module> module;
  std::unique_ptr<PloySema> sema;
  bool sema_ok{false};
};

AnalyzeResult AnalyzeSource(const std::string &src) {
  AnalyzeResult r;
  PloyLexer lexer(src, "<config_stringification_test>");
  PloyParser parser(lexer, r.diags);
  // Disable external package discovery — we are only validating that
  // sema accepts / records the CONFIG declaration, not that the
  // referenced environment actually exists on disk.
  parser.ParseModule();
  r.module = parser.TakeModule();
  if (!r.module) {
    return r;
  }
  PloySemaOptions opts;
  opts.enable_package_discovery = false;
  r.sema = std::make_unique<PloySema>(r.diags, opts);
  r.sema_ok = r.sema->Analyze(r.module);
  return r;
}

bool MessageContains(const Diagnostics &d, const std::string &needle) {
  for (const auto &diag : d.All()) {
    if (diag.message.find(needle) != std::string::npos)
      return true;
  }
  return false;
}

const VenvConfigDecl *FirstConfig(const std::shared_ptr<polyglot::ploy::Module> &m) {
  if (!m) return nullptr;
  for (const auto &stmt : m->declarations) {
    if (auto cfg = std::dynamic_pointer_cast<VenvConfigDecl>(stmt)) {
      return cfg.get();
    }
  }
  return nullptr;
}

} // namespace

// ----------------------------------------------------------------------------
// Registry: every advertised (lang, manager) pair resolves.
// ----------------------------------------------------------------------------

TEST_CASE("Config registry: every documented (lang, manager) pair resolves",
          "[ploy][config][registry]") {
  // Every entry exposed by AllConfigManagerEntries() must round-trip
  // through ResolveConfigManager() and produce the same enumerator.
  for (const auto &entry : AllConfigManagerEntries()) {
    auto resolved = ResolveConfigManager(entry.language, entry.manager_name);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->kind == entry.kind);
    REQUIRE(resolved->language == entry.language);
    REQUIRE(resolved->manager_name == entry.manager_name);
  }
}

TEST_CASE("Config registry: unknown (lang, manager) pair returns nullopt",
          "[ploy][config][registry]") {
  REQUIRE_FALSE(ResolveConfigManager("python", "npm").has_value());
  REQUIRE_FALSE(ResolveConfigManager("rust", "venv").has_value());
  REQUIRE_FALSE(ResolveConfigManager("invented_lang", "venv").has_value());
}

TEST_CASE("Config registry: legacy keyword translation",
          "[ploy][config][registry]") {
  REQUIRE(LegacyConfigKeywordToManagerName("VENV").value() == "venv");
  REQUIRE(LegacyConfigKeywordToManagerName("conda").value() == "conda");
  REQUIRE(LegacyConfigKeywordToManagerName("UV").value() == "uv");
  REQUIRE(LegacyConfigKeywordToManagerName("PIPENV").value() == "pipenv");
  REQUIRE(LegacyConfigKeywordToManagerName("Poetry").value() == "poetry");
  REQUIRE_FALSE(LegacyConfigKeywordToManagerName("NPM").has_value());
  REQUIRE_FALSE(LegacyConfigKeywordToManagerName("CARGO").has_value());
}

// ----------------------------------------------------------------------------
// New stringified form is parsed and accepted for every registered manager.
// ----------------------------------------------------------------------------

TEST_CASE("CONFIG python \"venv\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG python "venv" ".venv";)");
  REQUIRE(r.module);
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->is_legacy_form == false);
  REQUIRE(cfg->language == "python");
  REQUIRE(cfg->manager_name == "venv");
  REQUIRE(cfg->venv_path == ".venv");
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kVenv);
  REQUIRE(r.sema_ok);
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("CONFIG python \"conda\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG python "conda" "myenv";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kConda);
  REQUIRE(r.sema_ok);
}

TEST_CASE("CONFIG rust \"cargo\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG rust "cargo" ".";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kCargo);
  REQUIRE(cfg->language == "rust");
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("CONFIG javascript \"npm\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG javascript "npm" "./node_modules";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kNpm);
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("CONFIG java \"maven\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG java "maven" "./pom.xml";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kMaven);
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("CONFIG dotnet \"nuget\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG dotnet "nuget" "./packages";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kNuget);
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("CONFIG ruby \"bundler\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG ruby "bundler" "./Gemfile";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kBundler);
  REQUIRE_FALSE(r.diags.HasErrors());
}

TEST_CASE("CONFIG go \"gomod\" is accepted by sema", "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG go "gomod" "./go.mod";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kGoMod);
  REQUIRE_FALSE(r.diags.HasErrors());
}

// ----------------------------------------------------------------------------
// Unregistered (lang, manager) pair is rejected by sema with a fix-it
// pointing at the canonical form.
// ----------------------------------------------------------------------------

TEST_CASE("CONFIG python \"npm\" is rejected as unknown manager",
          "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG python "npm" "./node_modules";)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "unknown package manager 'npm'"));
}

TEST_CASE("CONFIG rust \"venv\" is rejected as unknown manager",
          "[ploy][config]") {
  auto r = AnalyzeSource(R"(CONFIG rust "venv" ".venv";)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "unknown package manager 'venv'"));
}

// ----------------------------------------------------------------------------
// Legacy form: still accepted, but sema emits a deprecation warning.
// ----------------------------------------------------------------------------

TEST_CASE("Legacy CONFIG VENV is accepted with a deprecation warning",
          "[ploy][config][deprecation]") {
  auto r = AnalyzeSource(R"(CONFIG VENV python ".venv";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->is_legacy_form == true);
  REQUIRE(cfg->manager_name == "venv");
  REQUIRE(cfg->manager == VenvConfigDecl::ManagerKind::kVenv);
  REQUIRE(r.sema_ok);
  // Deprecation surfaces as a warning, not an error.
  REQUIRE_FALSE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "legacy `CONFIG <KEYWORD>` form is deprecated"));
}

TEST_CASE("Legacy CONFIG CONDA emits the same deprecation warning",
          "[ploy][config][deprecation]") {
  auto r = AnalyzeSource(R"(CONFIG CONDA python "myenv";)");
  REQUIRE_FALSE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "legacy `CONFIG <KEYWORD>` form is deprecated"));
}

TEST_CASE("Legacy CONFIG VENV without explicit language defaults to python",
          "[ploy][config][deprecation]") {
  auto r = AnalyzeSource(R"(CONFIG VENV ".venv";)");
  const auto *cfg = FirstConfig(r.module);
  REQUIRE(cfg != nullptr);
  REQUIRE(cfg->language == "python");
  REQUIRE(cfg->is_legacy_form == true);
}

// ----------------------------------------------------------------------------
// Parser-level rejections for malformed canonical form.
// ----------------------------------------------------------------------------

TEST_CASE("CONFIG without manager string is rejected", "[ploy][config][parse]") {
  // Identifier after CONFIG must be followed by a string literal naming
  // the package manager — a bare `;` here is rejected.
  auto r = AnalyzeSource(R"(CONFIG python;)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "expected string package-manager name"));
}

TEST_CASE("CONFIG with manager string but no path is rejected",
          "[ploy][config][parse]") {
  auto r = AnalyzeSource(R"(CONFIG python "venv";)");
  REQUIRE(r.diags.HasErrors());
  REQUIRE(MessageContains(r.diags, "expected string path"));
}
