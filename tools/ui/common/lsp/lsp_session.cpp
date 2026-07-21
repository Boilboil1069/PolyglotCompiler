/**
 * @file     lsp_session.cpp
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/lsp/lsp_session.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace polyglot::tools::ui::lsp {

namespace {

std::string CanonicalLanguageId(std::string language_id) {
  std::string folded = language_id;
  std::transform(folded.begin(), folded.end(), folded.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (folded == "poly" || folded == "ploy") return "poly";
  return language_id;
}

SessionKey CanonicalSessionKey(SessionKey key) {
  key.language_id = CanonicalLanguageId(std::move(key.language_id));
  return key;
}

}  // namespace

std::string LspSessionRegistry::MakeId(const SessionKey &key) {
  const SessionKey canonical = CanonicalSessionKey(key);
  return canonical.language_id + "@" + canonical.workspace_uri;
}

std::shared_ptr<LspSession> LspSessionRegistry::GetOrCreate(
    const SessionKey &key,
    const std::function<std::shared_ptr<LspClient>()> &make_client) {
  std::lock_guard<std::mutex> lock(mu_);
  const SessionKey canonical = CanonicalSessionKey(key);
  auto it = sessions_.find(canonical);
  if (it != sessions_.end()) return it->second;
  auto session = std::make_shared<LspSession>();
  session->client = make_client();
  session->id = MakeId(canonical);
  sessions_[canonical] = session;
  return session;
}

std::shared_ptr<LspSession> LspSessionRegistry::Find(const SessionKey &key) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = sessions_.find(CanonicalSessionKey(key));
  if (it == sessions_.end()) return nullptr;
  return it->second;
}

void LspSessionRegistry::Drop(const SessionKey &key) {
  std::shared_ptr<LspSession> session;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sessions_.find(CanonicalSessionKey(key));
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
