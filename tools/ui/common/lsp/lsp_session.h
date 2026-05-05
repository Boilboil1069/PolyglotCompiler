/**
 * @file     lsp_session.h
 * @brief    Multi-server session registry, keyed on (workspace_uri, language_id)
 *
 * Owns the lifetime of one @ref LspClient per (workspace, language)
 * pair, plus the negotiated capability cache.  This file defines the
 * pure-C++ portion of the registry; the Qt-side wiring (debouncing
 * editor changes, building stdio transports backed by QProcess) lives
 * in `mainwindow.cpp` and `lsp_log_panel.cpp`.
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "tools/ui/common/lsp/lsp_capability_registry.h"
#include "tools/ui/common/lsp/lsp_client.h"

namespace polyglot::tools::ui::lsp {

/// Composite key for the session map.
struct SessionKey {
  std::string workspace_uri;
  std::string language_id;

  bool operator==(const SessionKey &other) const noexcept {
    return workspace_uri == other.workspace_uri && language_id == other.language_id;
  }
};

struct SessionKeyHash {
  std::size_t operator()(const SessionKey &k) const noexcept {
    return std::hash<std::string>{}(k.workspace_uri) ^
           (std::hash<std::string>{}(k.language_id) << 1);
  }
};

/// Live LSP session: client + capability cache entry id.
struct LspSession {
  std::shared_ptr<LspClient> client;
  std::string id;          ///< Stable id used as the key in @ref LspCapabilityRegistry.
  bool initialized{false};
};

/// Registry that owns sessions, indexed by (workspace_uri, language_id).
class LspSessionRegistry {
 public:
  /// Get or create a session.  The factory @p make_client is invoked
  /// only when the session does not yet exist; it must return a fully
  /// wired client (transport already attached).
  std::shared_ptr<LspSession> GetOrCreate(
      const SessionKey &key,
      const std::function<std::shared_ptr<LspClient>()> &make_client);

  /// Look up an existing session, returning nullptr when absent.
  std::shared_ptr<LspSession> Find(const SessionKey &key) const;

  /// Drop and shut down a session.  Sends `shutdown` + `exit` if the
  /// session has been initialized.
  void Drop(const SessionKey &key);

  /// Shut down every session.  Used during application teardown.
  void DropAll();

  LspCapabilityRegistry &Capabilities() { return caps_; }

 private:
  static std::string MakeId(const SessionKey &key);

  mutable std::mutex mu_;
  std::unordered_map<SessionKey, std::shared_ptr<LspSession>, SessionKeyHash> sessions_;
  LspCapabilityRegistry caps_;
};

}  // namespace polyglot::tools::ui::lsp
