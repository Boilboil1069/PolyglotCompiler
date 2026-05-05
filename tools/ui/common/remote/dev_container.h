/**
 * @file     dev_container.h
 * @brief    `.devcontainer/devcontainer.json` parser + provisioner.
 *
 * The dev-container layer turns a project's `devcontainer.json`
 * into a `RemoteDescriptor` that points at a container running
 * the requested image, plus a `ProvisionPlan` that lists every
 * tool to install once the container is up (polyls, language LSPs,
 * package managers).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tools/ui/common/remote/remote_session.h"

namespace polyglot::tools::ui::remote {

struct DevContainerSpec {
  std::string name;
  std::string image;                    ///< e.g. "mcr.microsoft.com/...".
  std::string dockerfile;               ///< Optional build context.
  std::string workspace_folder;         ///< Default `/workspaces/<name>`.
  std::string remote_user;
  std::vector<std::string> forward_ports;  ///< ["3000","8080:80",...].
  std::vector<std::string> post_create_commands;
  std::unordered_map<std::string, std::string> features;
  std::unordered_map<std::string, std::string> remote_env;
};

struct ProvisionPlan {
  std::vector<std::string> install_commands;  ///< Shell commands.
  std::vector<std::string> tools;             ///< polyls, npm, pip, ...
};

class DevContainer {
 public:
  /// Parse a `devcontainer.json` document.  Returns std::nullopt on
  /// failure.
  static std::optional<DevContainerSpec> Parse(const std::string &json);

  /// Build a remote descriptor that targets a container created
  /// from `spec`.
  static RemoteDescriptor MakeDescriptor(const DevContainerSpec &spec,
                                         const std::string &runtime = "docker",
                                         const std::string &container_id = {});

  /// Generate the post-create provisioning plan: install polyls,
  /// the requested LSPs and any explicit `postCreateCommand`.
  static ProvisionPlan MakeProvisionPlan(const DevContainerSpec &spec);
};

}  // namespace polyglot::tools::ui::remote
