/**
 * @file     package_manager_test.cpp
 * @brief    Unit tests for backends + service.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/packages/package_manager.h"

using namespace polyglot::tools::ui::packages;

TEST_CASE("Registry exposes all twelve backends",
          "[polyui][packages]") {
  PackageManagerRegistry r;
  CHECK(r.All().size() == 12);
  CHECK(r.Get(Ecosystem::kCargo) != nullptr);
  CHECK(r.FindByManifest("Cargo.toml") != nullptr);
  CHECK(r.FindByManifest("package.json")->ecosystem() == Ecosystem::kNpm);
  CHECK(r.FindByManifest("does-not-exist") == nullptr);
}

TEST_CASE("EcosystemName/EcosystemFromName round-trip",
          "[polyui][packages]") {
  for (auto e : {Ecosystem::kVenv, Ecosystem::kCargo, Ecosystem::kGoMod}) {
    CHECK(*EcosystemFromName(EcosystemName(e)) == e);
  }
  CHECK_FALSE(EcosystemFromName("rubbish"));
}

TEST_CASE("Pip lockfile parsing", "[polyui][packages][pip]") {
  PackageManagerRegistry r;
  auto pkgs = r.Get(Ecosystem::kVenv)->ParseLockfile(
      "# comment\nrequests==2.31.0\nflask==3.0.0 ; python_version > '3.8'\n");
  REQUIRE(pkgs.size() == 2);
  CHECK(pkgs[0].name == "requests");
  CHECK(pkgs[0].version == "2.31.0");
  CHECK(pkgs[1].name == "flask");
  CHECK(pkgs[1].version == "3.0.0");
}

TEST_CASE("Cargo Cargo.lock parsing", "[polyui][packages][cargo]") {
  PackageManagerRegistry r;
  auto pkgs = r.Get(Ecosystem::kCargo)->ParseLockfile(
      "[[package]]\nname = \"serde\"\nversion = \"1.0.197\"\n"
      "source = \"registry+https://github.com/rust-lang/crates.io-index\"\n"
      "[[package]]\nname = \"tokio\"\nversion = \"1.36.0\"\n");
  REQUIRE(pkgs.size() == 2);
  CHECK(pkgs[0].name == "serde");
  CHECK(pkgs[0].version == "1.0.197");
  CHECK(pkgs[1].name == "tokio");
}

TEST_CASE("npm package-lock.json parsing", "[polyui][packages][npm]") {
  PackageManagerRegistry r;
  std::string lock = R"({
    "lockfileVersion": 3,
    "packages": {
      "": { "name": "root", "version": "0.0.0" },
      "node_modules/lodash": { "version": "4.17.21" },
      "node_modules/express": { "version": "4.19.2" }
    }
  })";
  auto pkgs = r.Get(Ecosystem::kNpm)->ParseLockfile(lock);
  REQUIRE(pkgs.size() == 2);
  std::vector<std::string> names{pkgs[0].name, pkgs[1].name};
  CHECK(std::find(names.begin(), names.end(), "lodash") != names.end());
  CHECK(std::find(names.begin(), names.end(), "express") != names.end());
}

TEST_CASE("Maven pom.xml dependency parsing",
          "[polyui][packages][maven]") {
  PackageManagerRegistry r;
  std::string pom = R"(<project>
    <dependencies>
      <dependency>
        <groupId>com.google.guava</groupId>
        <artifactId>guava</artifactId>
        <version>32.1.3-jre</version>
      </dependency>
    </dependencies>
  </project>)";
  auto pkgs = r.Get(Ecosystem::kMaven)->ParseLockfile(pom);
  REQUIRE(pkgs.size() == 1);
  CHECK(pkgs[0].name == "com.google.guava:guava");
  CHECK(pkgs[0].version == "32.1.3-jre");
}

TEST_CASE("go.sum parsing collapses go.mod trailers",
          "[polyui][packages][gomod]") {
  PackageManagerRegistry r;
  std::string sum =
      "github.com/spf13/cobra v1.8.0 h1:hash\n"
      "github.com/spf13/cobra v1.8.0/go.mod h1:hash\n"
      "github.com/stretchr/testify v1.9.0 h1:hash\n";
  auto pkgs = r.Get(Ecosystem::kGoMod)->ParseLockfile(sum);
  REQUIRE(pkgs.size() == 2);
  CHECK(pkgs[0].name == "github.com/spf13/cobra");
  CHECK(pkgs[0].version == "v1.8.0");
}

TEST_CASE("Service routes install/upgrade/remove through the executor",
          "[polyui][packages][service]") {
  std::vector<std::vector<std::string>> calls;
  auto exec = [&](const std::vector<std::string> &argv,
                  const std::string &) {
    calls.push_back(argv);
    return CommandResult{0, "ok", ""};
  };
  PackageManagerService svc(exec);
  Environment env;
  env.ecosystem = Ecosystem::kCargo;
  env.root = "/tmp/proj";
  CHECK(svc.Install(env, "serde", "1.0.197").exit_code == 0);
  CHECK(svc.Upgrade(env, "tokio").exit_code == 0);
  CHECK(svc.Remove(env, "rand").exit_code == 0);
  REQUIRE(calls.size() == 3);
  CHECK(calls[0][0] == "cargo");
  CHECK(calls[0][1] == "add");
  CHECK(calls[2][1] == "remove");
}

TEST_CASE("SyncWithConfig reports missing requirements both ways",
          "[polyui][packages][config]") {
  PackageManagerService svc(nullptr);
  Environment env;
  env.ecosystem = Ecosystem::kCargo;
  std::vector<std::string> config = {"serde==1.0.197", "anyhow==1.0.80"};
  std::vector<Package> resolved;
  Package p1; p1.name = "serde"; p1.version = "1.0.197"; resolved.push_back(p1);
  Package p2; p2.name = "tokio"; p2.version = "1.36.0"; resolved.push_back(p2);

  auto r = svc.SyncWithConfig(env, config, resolved);
  // anyhow is in CONFIG but missing from lockfile.
  REQUIRE(r.missing_in_lockfile.size() == 1);
  CHECK(r.missing_in_lockfile[0] == "anyhow==1.0.80");
  // tokio is in lockfile but not in CONFIG.
  REQUIRE(r.missing_in_config.size() == 1);
  CHECK(r.missing_in_config[0] == "tokio==1.36.0");
}
