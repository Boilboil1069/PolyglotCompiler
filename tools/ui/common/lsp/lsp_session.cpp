/**
 * @file     lsp_session.cpp
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/lsp/lsp_session.h"

namespace polyglot::tools::ui::lsp {

std::string LspSessionRegistry::MakeId(const SessionKey &key) {
  return key.language_id + "@" + key.workspace_uri;
}

std::shared_ptr<LspSession> LspSessionRegistry::GetOrCreate(
    const SessionKey &key,
    const std::function<std::shared_ptr<LspClient>()> &make_client) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = sessions_.find(key);
  if (it != sessions_.end()) return it->second;
  auto session = std::make_shared<LspSession>();
  session->client = make_client();
  session->id = MakeId(key);
  sessions_[key] = session;
  return session;
}

std::shared_ptr<LspSession> LspSessionRegistry::Find(const SessionKey &key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = sessions_.find(key);
  if (it == sessions_.end()) return nullptr;
  return it->second;
}

void LspSessionRegistry::Drop(const SessionKey &key) {
  std::shared_ptr<LspSession> session;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return;
    session = it->second;
    sessions_.erase(it);
  }
  if (session && session->client && session->initialized) {
    session->client->Shutdown([](const Json &, const Json &) {});
    session->client->Exit();
  }
  caps_.Remove(session ? session->id : std::string{});
}

void LspSessionRegistry::DropAll() {
  std::unordered_map<SessionKey, std::shared_ptr<LspSession>, SessionKeyHash> snap;
  {
    std::lock_guard<std::mutex> lock(mu_);
    snap.swap(sessions_);
  }
  for (auto &kv : snap) {
    auto &session = kv.second;
    if (session && session->client && session->initialized) {
      session->client->Shutdown([](const Json &, const Json &) {});
      session->client->Exit();
    }
  }
  caps_.Clear();
}

}  // namespace polyglot::tools::ui::lsp
