/**
 * @file     target_backend_registry_test.cpp
 * @brief    Unit tests for ITargetBackend / BackendRegistry
 *
 * These tests exercise behaviour-level guarantees of the backend registry
 * introduced in version 1.3.3:
 *   1. The three production backends (x86_64, arm64, wasm32) auto-register
 *      themselves via REGISTER_TARGET_BACKEND() at static init time.
 *   2. Find() resolves both canonical triples and registered aliases (case
 *      insensitive).
 *   3. List() exposes a stable, sorted view with non-empty descriptions
 *      and capability matrices.
 *   4. Duplicate triples and alias collisions are rejected by Register()
 *      with the documented RegisterStatus values.
 *   5. ToJson() / ToHumanReadable() emit non-empty, valid output for every
 *      registered backend.
 *
 * @ingroup  Tests / Backends / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "backends/common/include/backend_registry.h"
#include "backends/common/include/target_backend.h"
#include "middle/include/ir/ir_context.h"

using polyglot::backends::BackendCapabilities;
using polyglot::backends::BackendInfo;
using polyglot::backends::BackendRegistry;
using polyglot::backends::CompileResult;
using polyglot::backends::ITargetBackend;
using polyglot::backends::MakeBackendInfo;
using polyglot::backends::RegisterStatus;
using polyglot::backends::TargetOptions;

namespace {

// A minimal hand-rolled backend used to exercise duplicate / collision paths.
class FakeBackend final : public ITargetBackend {
public:
  FakeBackend(std::string triple, std::vector<std::string> aliases)
      : triple_(std::move(triple)), aliases_(std::move(aliases)) {}

  std::string TargetTriple() const override { return triple_; }
  std::string Description() const override { return "fake backend used by unit tests"; }
  std::vector<std::string> Aliases() const override { return aliases_; }
  BackendCapabilities Capabilities() const override {
    BackendCapabilities caps;
    caps.emits_object = false;
    caps.emits_assembly = true;
    return caps;
  }
  CompileResult Compile(const polyglot::ir::IRContext &, const TargetOptions &) override {
    CompileResult r;
    r.ok = true;
    return r;
  }

private:
  std::string triple_;
  std::vector<std::string> aliases_;
};

} // namespace

TEST_CASE("BackendRegistry auto-registers all production backends",
          "[backends][registry]") {
  auto &registry = BackendRegistry::Instance();

  // The three production backends must be present after static init.
  REQUIRE(registry.Find("x86_64-unknown-elf") != nullptr);
  REQUIRE(registry.Find("aarch64-unknown-elf") != nullptr);
  REQUIRE(registry.Find("wasm32-unknown-unknown") != nullptr);
  REQUIRE(registry.Size() >= 3u);
}

TEST_CASE("BackendRegistry resolves aliases case-insensitively",
          "[backends][registry]") {
  auto &registry = BackendRegistry::Instance();

  // x86_64 aliases.
  REQUIRE(registry.Find("amd64") != nullptr);
  REQUIRE(registry.Find("x86-64") != nullptr);
  REQUIRE(registry.Find("X64") != nullptr);
  REQUIRE(registry.Find("x86_64") == registry.Find("amd64"));

  // ARM64 aliases.
  REQUIRE(registry.Find("arm64") != nullptr);
  REQUIRE(registry.Find("AArch64") != nullptr);
  REQUIRE(registry.Find("armv8") == registry.Find("aarch64-unknown-elf"));

  // WASM aliases.
  REQUIRE(registry.Find("wasm") != nullptr);
  REQUIRE(registry.Find("WASM32") != nullptr);
  REQUIRE(registry.Find("wasm32") == registry.Find("wasm32-unknown-unknown"));
}

TEST_CASE("BackendRegistry::List exposes stable sorted snapshot",
          "[backends][registry]") {
  auto infos = BackendRegistry::Instance().List();
  REQUIRE(infos.size() >= 3u);

  // Sorted ascending by triple.
  for (std::size_t i = 1; i < infos.size(); ++i) {
    REQUIRE(infos[i - 1].triple < infos[i].triple);
  }

  bool saw_x86 = false;
  bool saw_arm = false;
  bool saw_wasm = false;
  for (const auto &info : infos) {
    REQUIRE_FALSE(info.description.empty());
    if (info.triple == "x86_64-unknown-elf") {
      REQUIRE(info.capabilities.emits_object);
      REQUIRE(info.capabilities.emits_assembly);
      REQUIRE(info.capabilities.supports_linear_scan);
      REQUIRE(info.capabilities.supports_graph_coloring);
      saw_x86 = true;
    } else if (info.triple == "aarch64-unknown-elf") {
      REQUIRE(info.capabilities.emits_object);
      REQUIRE(info.capabilities.emits_assembly);
      REQUIRE(info.capabilities.supports_linear_scan);
      saw_arm = true;
    } else if (info.triple == "wasm32-unknown-unknown") {
      REQUIRE(info.capabilities.emits_object);
      REQUIRE(info.capabilities.emits_assembly);
      // Bitcode is not yet wired up; the matrix must reflect that honestly.
      REQUIRE_FALSE(info.capabilities.emits_bitcode);
      saw_wasm = true;
    }
  }
  REQUIRE(saw_x86);
  REQUIRE(saw_arm);
  REQUIRE(saw_wasm);
}

TEST_CASE("BackendRegistry rejects duplicate triples and alias collisions",
          "[backends][registry]") {
  auto &registry = BackendRegistry::Instance();

  // Duplicate canonical triple — must be rejected without mutating state.
  auto dup = std::make_shared<FakeBackend>(std::string{"x86_64-unknown-elf"},
                                           std::vector<std::string>{});
  const auto before = registry.Size();
  REQUIRE(registry.Register(dup) == RegisterStatus::kDuplicateTriple);
  REQUIRE(registry.Size() == before);

  // Alias collision against an already-registered canonical triple.
  auto alias_clash = std::make_shared<FakeBackend>(
      std::string{"fake-arch-1"}, std::vector<std::string>{"amd64"});
  REQUIRE(registry.Register(alias_clash) == RegisterStatus::kAliasConflict);
  REQUIRE(registry.Find("fake-arch-1") == nullptr);
  REQUIRE(registry.Size() == before);

  // Null backend pointer must be rejected with the dedicated status.
  REQUIRE(registry.Register(nullptr) == RegisterStatus::kNullBackend);
  REQUIRE(registry.Size() == before);
}

TEST_CASE("BackendRegistry::FindOrDiagnose surfaces available backends",
          "[backends][registry]") {
  std::string diag;
  auto *found =
      BackendRegistry::Instance().FindOrDiagnose("__definitely-not-a-target__", &diag);
  REQUIRE(found == nullptr);
  REQUIRE_FALSE(diag.empty());
  REQUIRE(diag.find("no backend registered") != std::string::npos);
  REQUIRE(diag.find("x86_64-unknown-elf") != std::string::npos);
  REQUIRE(diag.find("aarch64-unknown-elf") != std::string::npos);
}

TEST_CASE("BackendInfo serialises to JSON and human-readable text",
          "[backends][registry]") {
  auto *backend = BackendRegistry::Instance().Find("x86_64-unknown-elf");
  REQUIRE(backend != nullptr);
  auto info = MakeBackendInfo(*backend);

  std::string json = polyglot::backends::ToJson(info);
  REQUIRE_FALSE(json.empty());
  REQUIRE(json.front() == '{');
  REQUIRE(json.back() == '}');
  REQUIRE(json.find("\"triple\":\"x86_64-unknown-elf\"") != std::string::npos);
  REQUIRE(json.find("\"emits_object\":true") != std::string::npos);

  std::string human = polyglot::backends::ToHumanReadable(info);
  REQUIRE(human.find("triple") != std::string::npos);
  REQUIRE(human.find("x86_64-unknown-elf") != std::string::npos);
  REQUIRE(human.find("capabilities") != std::string::npos);
}

TEST_CASE("ITargetBackend bitcode default returns unsupported diagnostic",
          "[backends][registry]") {
  auto *backend = BackendRegistry::Instance().Find("x86_64-unknown-elf");
  REQUIRE(backend != nullptr);

  polyglot::ir::IRContext module;
  TargetOptions options;
  options.emit = polyglot::backends::EmitKind::kBitcode;
  CompileResult result = backend->EmitBitcode(module, options);
  REQUIRE_FALSE(result.ok);
  REQUIRE_FALSE(result.diagnostics.empty());
  REQUIRE(result.diagnostics.front().severity ==
          polyglot::backends::BackendDiagnostic::Severity::kError);
  REQUIRE(result.diagnostics.front().message.find("bitcode") != std::string::npos);
}
