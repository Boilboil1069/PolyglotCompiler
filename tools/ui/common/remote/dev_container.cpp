/**
 * @file     dev_container.cpp
 * @brief    Implementation of `dev_container.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/remote/dev_container.h"

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::remote {
namespace {

using Json = nlohmann::json;

/// Map a `features` key (e.g. "ghcr.io/devcontainers/features/python:1")
/// to the install command and recognised tool tag.
struct FeatureMapping {
  std::string feature_key_substring;
  std::string install_command;
  std::string tool;
};

const std::vector<FeatureMapping> &FeatureCatalogue() {
  static const std::vector<FeatureMapping> kCatalogue = {
      {"python",     "pip install --upgrade pip", "python-lsp"},
      {"node",       "npm install -g typescript-language-server", "ts-ls"},
      {"java",       "sdk install java",          "jdtls"},
      {"go",         "go install golang.org/x/tools/gopls@latest", "gopls"},
      {"rust",       "rustup component add rust-analyzer", "rust-analyzer"},
      {"dotnet",     "dotnet tool install -g csharp-ls", "csharp-ls"},
      {"ruby",       "gem install solargraph",    "solargraph"},
      {"cpp",        "apt-get install -y clangd", "clangd"},
  };
  return kCatalogue;
}

}  // namespace

std::optional<DevContainerSpec> DevContainer::Parse(const std::string &json) {
  auto j = Json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return std::nullopt;
  DevContainerSpec spec;
  spec.name = j.value("name", std::string{});
  spec.image = j.value("image", std::string{});
  spec.dockerfile = j.value("dockerFile", std::string{});
  spec.workspace_folder = j.value("workspaceFolder", std::string{});
  spec.remote_user = j.value("remoteUser", std::string{});

  if (j.contains("forwardPorts") && j["forwardPorts"].is_array()) {
    for (const auto &p : j["forwardPorts"]) {
      if (p.is_number_integer())
        spec.forward_ports.push_back(std::to_string(p.get<int>()));
      else if (p.is_string())
        spec.forward_ports.push_back(p.get<std::string>());
    }
  }
  if (j.contains("postCreateCommand")) {
    const auto &pcc = j["postCreateCommand"];
    if (pcc.is_string()) {
      spec.post_create_commands.push_back(pcc.get<std::string>());
    } else if (pcc.is_array()) {
      for (const auto &c : pcc)
        if (c.is_string())
          spec.post_create_commands.push_back(c.get<std::string>());
    }
  }
  if (j.contains("features") && j["features"].is_object()) {
    for (const auto &item : j["features"].items()) {
      std::string val;
      if (item.value().is_string())      val = item.value().get<std::string>();
      else if (item.value().is_object()) val = item.value().dump();
      else                                val = item.value().dump();
      spec.features[item.key()] = val;
    }
  }
  if (j.contains("remoteEnv") && j["remoteEnv"].is_object()) {
    for (const auto &item : j["remoteEnv"].items())
      if (item.value().is_string())
        spec.remote_env[item.key()] = item.value().get<std::string>();
  }
  return spec;
}

RemoteDescriptor DevContainer::MakeDescriptor(
    const DevContainerSpec &spec, const std::string &runtime,
    const std::string &container_id) {
  RemoteDescriptor d;
  d.kind = RemoteKind::kContainer;
  d.runtime = runtime;
  d.host = container_id.empty() ? spec.name : container_id;
  d.image = spec.image;
  d.user = spec.remote_user;
  d.workspace = spec.workspace_folder.empty()
      ? "/workspaces/" + spec.name
      : spec.workspace_folder;
  d.env = spec.remote_env;
  return d;
}

ProvisionPlan DevContainer::MakeProvisionPlan(const DevContainerSpec &spec) {
  ProvisionPlan plan;
  // polyls is always provisioned so the IDE has a language server
  // inside the container.
  plan.tools.push_back("polyls");
  plan.install_commands.push_back(
      "curl -fsSL https://polyglot.dev/install.sh | sh -s -- polyls");

  for (const auto &[key, value] : spec.features) {
    for (const auto &fm : FeatureCatalogue()) {
      if (key.find(fm.feature_key_substring) != std::string::npos) {
        plan.tools.push_back(fm.tool);
        plan.install_commands.push_back(fm.install_command);
        break;
      }
    }
  }
  for (const auto &cmd : spec.post_create_commands)
    plan.install_commands.push_back(cmd);
  return plan;
}

}  // namespace polyglot::tools::ui::remote
