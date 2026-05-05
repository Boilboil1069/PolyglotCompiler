/**
 * @file     dev_container_test.cpp
 * @brief    Unit tests for the dev-container parser/provisioner.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/remote/dev_container.h"

using namespace polyglot::tools::ui::remote;

TEST_CASE("Parse extracts every interesting field",
          "[polyui][remote][devcontainer]") {
  std::string doc = R"({
    "name":"polyglot-dev",
    "image":"mcr.microsoft.com/devcontainers/base:ubuntu",
    "workspaceFolder":"/workspaces/polyglot",
    "remoteUser":"vscode",
    "forwardPorts":[3000,"8080:80"],
    "postCreateCommand":["polyc --version","cmake --version"],
    "features":{
      "ghcr.io/devcontainers/features/python:1":"latest",
      "ghcr.io/devcontainers/features/rust:1":"stable"
    },
    "remoteEnv":{"POLY_FLAVOUR":"dev","TZ":"UTC"}
  })";
  auto spec = DevContainer::Parse(doc);
  REQUIRE(spec);
  CHECK(spec->name == "polyglot-dev");
  CHECK(spec->image == "mcr.microsoft.com/devcontainers/base:ubuntu");
  CHECK(spec->workspace_folder == "/workspaces/polyglot");
  CHECK(spec->remote_user == "vscode");
  REQUIRE(spec->forward_ports.size() == 2);
  CHECK(spec->forward_ports[0] == "3000");
  CHECK(spec->forward_ports[1] == "8080:80");
  CHECK(spec->post_create_commands.size() == 2);
  CHECK(spec->features.size() == 2);
  CHECK(spec->remote_env.at("POLY_FLAVOUR") == "dev");
}

TEST_CASE("MakeDescriptor falls back to defaults",
          "[polyui][remote][devcontainer]") {
  DevContainerSpec spec;
  spec.name = "poly-dev";
  spec.image = "polyglot/dev:latest";
  auto d = DevContainer::MakeDescriptor(spec);
  CHECK(d.kind == RemoteKind::kContainer);
  CHECK(d.runtime == "docker");
  CHECK(d.host == "poly-dev");
  CHECK(d.workspace == "/workspaces/poly-dev");
  CHECK(d.image == "polyglot/dev:latest");

  spec.workspace_folder = "/srv/poly";
  spec.remote_user = "vscode";
  auto d2 = DevContainer::MakeDescriptor(spec, "podman", "abc123");
  CHECK(d2.runtime == "podman");
  CHECK(d2.host == "abc123");
  CHECK(d2.workspace == "/srv/poly");
  CHECK(d2.user == "vscode");
}

TEST_CASE("MakeProvisionPlan provisions polyls + matches features",
          "[polyui][remote][devcontainer]") {
  DevContainerSpec spec;
  spec.features["ghcr.io/devcontainers/features/python:1"] = "latest";
  spec.features["ghcr.io/devcontainers/features/rust:1"]   = "stable";
  spec.post_create_commands.push_back("polyc samples build");

  auto plan = DevContainer::MakeProvisionPlan(spec);
  // polyls baseline + 2 features + 1 post-create command.
  CHECK(plan.tools.size() == 3);
  CHECK(plan.tools.front() == "polyls");
  bool saw_rust_analyzer = false, saw_python_lsp = false;
  for (const auto &t : plan.tools) {
    if (t == "rust-analyzer") saw_rust_analyzer = true;
    if (t == "python-lsp")    saw_python_lsp = true;
  }
  CHECK(saw_rust_analyzer);
  CHECK(saw_python_lsp);
  CHECK(plan.install_commands.back() == "polyc samples build");
}
