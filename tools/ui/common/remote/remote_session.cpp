/**
 * @file     remote_session.cpp
 * @brief    Implementation of `remote_session.h` plus four
 *           backends: LocalRemote, SshRemote, WslRemote,
 *           ContainerRemote.
 *
 * The backends share a common in-memory bookkeeping layer
 * (`RemoteSessionBase`) that owns the file table, process table
 * and port-forward table.  Concrete subclasses customise process
 * launching by recording the command line that *would* be issued
 * on the underlying transport — the IDE wires the actual transport
 * through the descriptor metadata.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/remote/remote_session.h"

#include <atomic>
#include <sstream>

namespace polyglot::tools::ui::remote {
namespace {

std::string NextProcessId() {
  static std::atomic<unsigned long long> counter{0};
  return "proc-" + std::to_string(++counter);
}

class RemoteSessionBase : public RemoteSession {
 public:
  explicit RemoteSessionBase(RemoteDescriptor desc) {
    descriptor_ = std::move(desc);
  }

  bool Connect() override        { connected_ = true; return true; }
  void Disconnect() override     { connected_ = false; }
  bool IsConnected() const override { return connected_; }

  bool WriteFile(const std::string &path,
                 const std::string &contents) override {
    files_[path] = contents;
    return true;
  }
  std::optional<std::string> ReadFile(const std::string &path) const override {
    auto it = files_.find(path);
    if (it == files_.end()) return std::nullopt;
    return it->second;
  }
  std::vector<RemoteFileStat> ListDir(
      const std::string &path) const override {
    std::vector<RemoteFileStat> out;
    std::string prefix = path;
    if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
    for (const auto &[p, body] : files_) {
      if (p.compare(0, prefix.size(), prefix) != 0) continue;
      RemoteFileStat s;
      s.path = p;
      s.is_dir = false;
      s.size = static_cast<long long>(body.size());
      out.push_back(std::move(s));
    }
    return out;
  }
  bool RemoveFile(const std::string &path) override {
    return files_.erase(path) > 0;
  }

  RemoteProcess Spawn(const std::string &command,
                      const std::vector<std::string> &args) override {
    RemoteProcess p;
    p.id = NextProcessId();
    p.command = TransformCommand(command);
    p.args = args;
    p.pid = next_pid_++;
    p.running = true;
    processes_[p.id] = p;
    return p;
  }
  bool Kill(const std::string &process_id) override {
    auto it = processes_.find(process_id);
    if (it == processes_.end()) return false;
    it->second.running = false;
    it->second.exit_code = -1;
    return true;
  }
  std::optional<RemoteProcess> Wait(const std::string &process_id) override {
    auto it = processes_.find(process_id);
    if (it == processes_.end()) return std::nullopt;
    it->second.running = false;
    return it->second;
  }

  PortForward Forward(int local_port, int remote_port,
                      const std::string &remote_host) override {
    PortForward f;
    f.local_port = local_port;
    f.remote_port = remote_port;
    f.remote_host = remote_host;
    f.active = true;
    forwards_[local_port] = f;
    return f;
  }
  bool Unforward(int local_port) override {
    auto it = forwards_.find(local_port);
    if (it == forwards_.end()) return false;
    it->second.active = false;
    forwards_.erase(it);
    return true;
  }
  std::vector<PortForward> ActiveForwards() const override {
    std::vector<PortForward> out;
    out.reserve(forwards_.size());
    for (const auto &[k, v] : forwards_) out.push_back(v);
    return out;
  }

  RemoteProcess OpenTerminal(const std::string &command) override {
    return Spawn(command.empty() ? DefaultShell() : command, {});
  }

 protected:
  /// Subclasses override to wrap commands in the appropriate
  /// transport invocation (ssh/wsl/docker exec).  Default is
  /// pass-through.
  virtual std::string TransformCommand(const std::string &command) const {
    return command;
  }
  virtual std::string DefaultShell() const { return "/bin/sh"; }

 private:
  bool connected_{false};
  int next_pid_{1000};
  std::unordered_map<std::string, std::string> files_;
  std::unordered_map<std::string, RemoteProcess> processes_;
  std::unordered_map<int, PortForward> forwards_;
};

class LocalRemote final : public RemoteSessionBase {
 public:
  explicit LocalRemote(RemoteDescriptor d)
      : RemoteSessionBase(std::move(d)) {}
};

class SshRemote final : public RemoteSessionBase {
 public:
  explicit SshRemote(RemoteDescriptor d)
      : RemoteSessionBase(std::move(d)) {}

 protected:
  std::string TransformCommand(const std::string &command) const override {
    std::ostringstream oss;
    oss << "ssh ";
    if (descriptor().port > 0) oss << "-p " << descriptor().port << " ";
    if (!descriptor().user.empty()) oss << descriptor().user << "@";
    oss << descriptor().host << " -- " << command;
    return oss.str();
  }
};

class WslRemote final : public RemoteSessionBase {
 public:
  explicit WslRemote(RemoteDescriptor d)
      : RemoteSessionBase(std::move(d)) {}

 protected:
  std::string TransformCommand(const std::string &command) const override {
    std::ostringstream oss;
    oss << "wsl -d " << descriptor().host << " -- " << command;
    return oss.str();
  }
  std::string DefaultShell() const override { return "/bin/bash"; }
};

class ContainerRemote final : public RemoteSessionBase {
 public:
  explicit ContainerRemote(RemoteDescriptor d)
      : RemoteSessionBase(std::move(d)) {}

 protected:
  std::string TransformCommand(const std::string &command) const override {
    std::string runtime = descriptor().runtime.empty()
        ? std::string{"docker"}
        : descriptor().runtime;
    std::ostringstream oss;
    oss << runtime << " exec ";
    if (!descriptor().user.empty()) oss << "-u " << descriptor().user << " ";
    oss << descriptor().host << " " << command;
    return oss.str();
  }
};

}  // namespace

std::string RemoteKindName(RemoteKind k) {
  switch (k) {
    case RemoteKind::kLocal:     return "local";
    case RemoteKind::kSsh:       return "ssh";
    case RemoteKind::kWsl:       return "wsl";
    case RemoteKind::kContainer: return "container";
  }
  return "unknown";
}

std::optional<RemoteKind> RemoteKindFromName(const std::string &name) {
  if (name == "local")     return RemoteKind::kLocal;
  if (name == "ssh")       return RemoteKind::kSsh;
  if (name == "wsl")       return RemoteKind::kWsl;
  if (name == "container") return RemoteKind::kContainer;
  return std::nullopt;
}

std::unique_ptr<RemoteSession> CreateSession(RemoteDescriptor desc) {
  switch (desc.kind) {
    case RemoteKind::kLocal:
      return std::make_unique<LocalRemote>(std::move(desc));
    case RemoteKind::kSsh:
      return std::make_unique<SshRemote>(std::move(desc));
    case RemoteKind::kWsl:
      return std::make_unique<WslRemote>(std::move(desc));
    case RemoteKind::kContainer:
      return std::make_unique<ContainerRemote>(std::move(desc));
  }
  return nullptr;
}

std::optional<RemoteDescriptor> ParseConnectionString(const std::string &uri) {
  RemoteDescriptor d;
  auto scheme = uri.find("://");
  if (scheme == std::string::npos) {
    // local:/path or local:relative
    if (uri.compare(0, 6, "local:") != 0) return std::nullopt;
    d.kind = RemoteKind::kLocal;
    d.workspace = uri.substr(6);
    return d;
  }
  std::string proto = uri.substr(0, scheme);
  std::string rest = uri.substr(scheme + 3);
  auto kind = RemoteKindFromName(proto);
  if (!kind) return std::nullopt;
  d.kind = *kind;

  if (*kind == RemoteKind::kSsh) {
    auto slash = rest.find('/');
    std::string authority = slash == std::string::npos
        ? rest
        : rest.substr(0, slash);
    d.workspace = slash == std::string::npos
        ? std::string{}
        : rest.substr(slash);
    auto at = authority.find('@');
    std::string hostport;
    if (at != std::string::npos) {
      d.user = authority.substr(0, at);
      hostport = authority.substr(at + 1);
    } else {
      hostport = authority;
    }
    auto colon = hostport.find(':');
    if (colon != std::string::npos) {
      d.host = hostport.substr(0, colon);
      d.port = std::stoi(hostport.substr(colon + 1));
    } else {
      d.host = hostport;
    }
    return d;
  }
  if (*kind == RemoteKind::kWsl) {
    auto slash = rest.find('/');
    d.host = slash == std::string::npos ? rest : rest.substr(0, slash);
    d.workspace = slash == std::string::npos
        ? std::string{}
        : rest.substr(slash);
    return d;
  }
  if (*kind == RemoteKind::kContainer) {
    // container://runtime/host/path  or  container://host/path
    auto first = rest.find('/');
    std::string head = first == std::string::npos
        ? rest
        : rest.substr(0, first);
    std::string tail = first == std::string::npos
        ? std::string{}
        : rest.substr(first + 1);
    if (head == "docker" || head == "podman") {
      d.runtime = head;
      auto second = tail.find('/');
      d.host = second == std::string::npos ? tail : tail.substr(0, second);
      d.workspace = second == std::string::npos
          ? std::string{}
          : tail.substr(second);
    } else {
      d.runtime = "docker";
      d.host = head;
      d.workspace = first == std::string::npos
          ? std::string{}
          : "/" + tail;
    }
    return d;
  }
  // local:// fall-through.
  d.workspace = rest;
  return d;
}

}  // namespace polyglot::tools::ui::remote
