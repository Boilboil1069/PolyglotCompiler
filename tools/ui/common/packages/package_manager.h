/**
 * @file     package_manager.h
 * @brief    Cross-ecosystem package-manager abstraction.
 *
 * Twelve ecosystems (`venv`, `conda`, `uv`, `pipenv`, `poetry`,
 * `cargo`, `npm`, `maven`, `gradle`, `nuget`, `gem`, `gomod`) are
 * unified behind one `PackageManagerBackend` interface.  Each
 * backend describes the commands the IDE shell must execute for
 * install / upgrade / remove operations, the manifest and lockfile
 * paths, and a parser for the lockfile contents.  Actual sub-process
 * execution is injected via `CommandExecutor`, keeping the value
 * model transport-agnostic and unit-testable.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::packages {

enum class Ecosystem {
  kVenv,
  kConda,
  kUv,
  kPipenv,
  kPoetry,
  kCargo,
  kNpm,
  kMaven,
  kGradle,
  kNuget,
  kGem,
  kGoMod,
};

std::string EcosystemName(Ecosystem e);
std::optional<Ecosystem> EcosystemFromName(const std::string &name);

struct Package {
  std::string name;
  std::string version;
  std::string source;       ///< e.g. "registry+https://...", "git+...".
  bool is_direct{false};    ///< Whether the package is a direct dep.
};

/// Result of running an external command.
struct CommandResult {
  int exit_code{0};
  std::string stdout_text;
  std::string stderr_text;
};

using CommandExecutor = std::function<CommandResult(
    const std::vector<std::string> &argv,
    const std::string &working_directory)>;

/// One concrete environment / project rooted at a directory.
struct Environment {
  Ecosystem ecosystem;
  std::string root;          ///< Absolute path of the project root.
  std::string display_name;  ///< Friendly name shown in the status bar.
  bool active{false};
};

/// Abstract backend.  Subclasses describe the commands and lockfile
/// shape for one ecosystem.
class PackageManagerBackend {
 public:
  virtual ~PackageManagerBackend() = default;

  virtual Ecosystem ecosystem() const = 0;
  virtual std::string manifest_filename() const = 0;
  virtual std::string lockfile_filename() const = 0;

  /// Argv to invoke when installing / upgrading / removing one
  /// package.  The caller appends the package name (and optional
  /// version) at the end.
  virtual std::vector<std::string> InstallArgv(
      const std::string &package, const std::string &version) const = 0;
  virtual std::vector<std::string> UpgradeArgv(
      const std::string &package) const = 0;
  virtual std::vector<std::string> RemoveArgv(
      const std::string &package) const = 0;

  /// Parse a lockfile body into the resolved package set.
  virtual std::vector<Package> ParseLockfile(
      const std::string &content) const = 0;

  /// Convert a `CONFIG` entry from a `.ploy` file into the canonical
  /// `name==version` (or ecosystem-native) requirement string.
  virtual std::string ToConfigRequirement(const Package &p) const;

  /// Inverse of `ToConfigRequirement`: split a `.ploy CONFIG`
  /// requirement string back into a name / version pair.
  virtual Package FromConfigRequirement(const std::string &requirement) const;
};

/// Registry of all built-in backends.  The IDE shell looks up by
/// `Ecosystem` or by manifest filename when discovering project
/// roots.
class PackageManagerRegistry {
 public:
  PackageManagerRegistry();

  PackageManagerBackend *Get(Ecosystem e) const;
  PackageManagerBackend *FindByManifest(const std::string &filename) const;
  std::vector<Ecosystem> All() const;

 private:
  std::unordered_map<int, std::unique_ptr<PackageManagerBackend>> by_eco_;
  std::unordered_map<std::string, PackageManagerBackend *> by_manifest_;
};

/// High-level facade used by the IDE shell.  Holds the registry,
/// the discovered environments and an injected `CommandExecutor`.
class PackageManagerService {
 public:
  explicit PackageManagerService(CommandExecutor executor);

  /// Scan a directory tree for known manifests and register the
  /// corresponding environments.  Returns the number of new
  /// environments added.
  std::size_t Discover(const std::string &workspace_root,
                       const std::vector<std::string> &candidate_dirs);

  /// Mark one environment active for the status bar.  At most one
  /// environment per ecosystem may be active.
  void Activate(const std::string &root);
  const Environment *ActiveFor(Ecosystem e) const;
  const std::vector<Environment> &environments() const { return envs_; }

  CommandResult Install(const Environment &env, const std::string &package,
                        const std::string &version = "");
  CommandResult Upgrade(const Environment &env, const std::string &package);
  CommandResult Remove(const Environment &env, const std::string &package);

  /// Parse the lockfile for an environment using the backend for
  /// its ecosystem.  Returns `std::nullopt` when no lockfile exists.
  std::optional<std::vector<Package>> ReadLockfile(
      const Environment &env,
      const std::function<std::optional<std::string>(const std::string &)>
          &reader) const;

  /// Project the `CONFIG` block of a `.ploy` document against the
  /// resolved lockfile contents.  Returns the requirements that the
  /// `.ploy CONFIG` declares but the lockfile does not yet pin, and
  /// vice versa.
  struct ConfigSyncReport {
    std::vector<std::string> missing_in_lockfile;
    std::vector<std::string> missing_in_config;
  };
  ConfigSyncReport SyncWithConfig(
      const Environment &env, const std::vector<std::string> &config_block,
      const std::vector<Package> &resolved) const;

  PackageManagerRegistry &registry() { return registry_; }

 private:
  CommandExecutor executor_;
  PackageManagerRegistry registry_;
  std::vector<Environment> envs_;
};

}  // namespace polyglot::tools::ui::packages
