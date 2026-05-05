/**
 * @file     lsp_capability_registry.h
 * @brief    Cache of negotiated server capabilities, keyed by session id
 *
 * Stores @ref ServerCapabilities returned by each language server's
 * `initialize` response so that the editor can quickly check which
 * features are available without re-querying the server.
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "tools/ui/common/lsp/lsp_message.h"

namespace polyglot::tools::ui::lsp {

/// Thread-safe map from session id (any opaque string) to the server's
/// negotiated @ref ServerCapabilities.
class LspCapabilityRegistry {
 public:
  void Set(const std::string &session_id, const ServerCapabilities &caps);
  std::optional<ServerCapabilities> Get(const std::string &session_id) const;
  void Remove(const std::string &session_id);
  void Clear();

  /// Convenience: query a single capability on a session.  Returns false
  /// when the session is unknown.
  bool Supports(const std::string &session_id,
                bool ServerCapabilities::*flag) const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, ServerCapabilities> caps_;
};

}  // namespace polyglot::tools::ui::lsp
