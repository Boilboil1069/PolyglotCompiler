/**
 * @file     lsp_capability_registry.cpp
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/lsp/lsp_capability_registry.h"

namespace polyglot::tools::ui::lsp {

void LspCapabilityRegistry::Set(const std::string &session_id,
                                const ServerCapabilities &caps) {
  std::lock_guard<std::mutex> lock(mu_);
  caps_[session_id] = caps;
}

std::optional<ServerCapabilities> LspCapabilityRegistry::Get(
    const std::string &session_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = caps_.find(session_id);
  if (it == caps_.end()) return std::nullopt;
  return it->second;
}

void LspCapabilityRegistry::Remove(const std::string &session_id) {
  std::lock_guard<std::mutex> lock(mu_);
  caps_.erase(session_id);
}

void LspCapabilityRegistry::Clear() {
  std::lock_guard<std::mutex> lock(mu_);
  caps_.clear();
}

bool LspCapabilityRegistry::Supports(const std::string &session_id,
                                     bool ServerCapabilities::*flag) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = caps_.find(session_id);
  if (it == caps_.end()) return false;
  return it->second.*flag;
}

}  // namespace polyglot::tools::ui::lsp
