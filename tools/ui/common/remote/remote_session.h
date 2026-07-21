/**
 * @file     remote_session.h
 * @brief    Remote-development session abstraction.
 *
 * `RemoteSession` is the unified interface every IDE subsystem
 * (polyls, DAP, task runner, terminal) talks to.  The IDE never
 * branches on "is this local?" — local execution is just the
 * `LocalRemote` implementation.
 *
 * Concrete backends (`LocalRemote`, `SshRemote`, `WslRemote`,
 * `ContainerRemote`) describe themselves through a common
 * descriptor so the orchestrator can pick the right backend from a
 * single connection string.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::remote {

enum class RemoteKind {
  kLocal,
  kSsh,
  kWsl,
  kContainer,
};

std::string RemoteKindName(RemoteKind k);
std::optional<RemoteKind> RemoteKindFromName(const std::string &name);

struct RemoteDescriptor {
  RemoteKind kind{RemoteKind::kLocal};
  std::string host;          ///< SSH host, WSL distro name, container id.
  std::string user;          ///< Optional user (SSH / container).
  std::string workspace;     ///< Workspace path on the remote side.
  int port{0};               ///< SSH port (0 = default 22).
  std::string runtime;       ///< "docker" / "podman" for kContainer.
  std::string image;         ///< Container image (kContainer only).
  std::unordered_map<std::string, std::string> env;
};

struct RemoteFileStat {
  std::string path;
  bool is_dir{false};
  long long size{0};
  long long mtime{0};
};

struct RemoteProcess {
  std::string id;            ///< Backend-assigned process id.
  std::string command;
  std::vector<std::string> args;
  int pid{0};
  bool running{false};
  int exit_code{0};
  std::string stdout_buf;
  std::string stderr_buf;
};

struct PortForward {
  int local_port{0};
  int remote_port{0};
  std::string remote_host{"localhost"};
  bool active{false};
};

class RemoteSession {
 public:
  virtual ~RemoteSession() = default;

  const RemoteDescriptor &descriptor() const { return descriptor_; }

  /// Establish the connection (no-op for `LocalRemote`).  Returns
  /// false on failure.
  virtual bool Connect() = 0;
  virtual void Disconnect() = 0;
  virtual bool IsConnected() const = 0;

  /// Filesystem operations.
  virtual bool WriteFile(const std::string &path,
                         const std::string &contents) = 0;
  virtual std::optional<std::string> ReadFile(
      const std::string &path) const = 0;
  virtual std::vector<RemoteFileStat> ListDir(
      const std::string &path) const = 0;
  virtual bool RemoveFile(const std::string &path) = 0;

  /// Process operations.
  virtual RemoteProcess Spawn(const std::string &command,
                              const std::vector<std::string> &args) = 0;
  virtual bool Kill(const std::string &process_id) = 0;
  virtual std::optional<RemoteProcess> Wait(
      const std::string &process_id) = 0;

  /// Port forwarding.
  virtual PortForward Forward(int local_port, int remote_port,
                              const std::string &remote_host = "localhost") = 0;
  virtual bool Unforward(int local_port) = 0;
  virtual std::vector<PortForward> ActiveForwards() const = 0;

  /// Open a remote shell channel.  `command` is empty for the
  /// default shell.  Returns the spawned process descriptor.
  virtual RemoteProcess OpenTerminal(const std::string &command = {}) = 0;

 protected:
  RemoteDescriptor descriptor_{};
};

/// Pick the matching backend from a descriptor.
std::unique_ptr<RemoteSession> CreateSession(RemoteDescriptor desc);

/// Parse a connection string into a descriptor.  Recognised forms:
///   `local:/path`
///   `ssh://[user@]host[:port]/path`
///   `wsl://distro/path`
///   `container://[runtime/]image-or-id/path`
std::optional<RemoteDescriptor> ParseConnectionString(const std::string &uri);

}  // namespace polyglot::tools::ui::remote
