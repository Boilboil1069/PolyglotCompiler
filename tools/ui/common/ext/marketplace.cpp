/**
 * @file     marketplace.cpp
 * @brief    Implementation of the local marketplace.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/ext/marketplace.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::ext {

using Json = nlohmann::json;

std::string MarketplaceSourceName(MarketplaceSource s) {
  switch (s) {
    case MarketplaceSource::kFilesystem: return "filesystem";
    case MarketplaceSource::kHttp:       return "http";
  }
  return "filesystem";
}

void MarketplaceIndex::Add(MarketplaceEntry entry) {
  auto &bucket = entries_[entry.id];
  bucket.push_back(std::move(entry));
  std::sort(bucket.begin(), bucket.end(),
            [](const MarketplaceEntry &a, const MarketplaceEntry &b) {
              return CompareVersion(a.version, b.version) > 0;
            });
}

std::vector<MarketplaceEntry> MarketplaceIndex::List() const {
  std::vector<MarketplaceEntry> out;
  for (const auto &kv : entries_)
    if (!kv.second.empty()) out.push_back(kv.second.front());
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  return out;
}

std::optional<MarketplaceEntry> MarketplaceIndex::Find(
    const std::string &id) const {
  auto it = entries_.find(id);
  if (it == entries_.end() || it->second.empty()) return std::nullopt;
  return it->second.front();
}

std::optional<MarketplaceEntry> MarketplaceIndex::FindVersion(
    const std::string &id, const std::string &version) const {
  auto it = entries_.find(id);
  if (it == entries_.end()) return std::nullopt;
  for (const auto &e : it->second)
    if (e.version == version) return e;
  return std::nullopt;
}

std::optional<MarketplaceIndex> ParseIndex(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return std::nullopt;
  }
  if (!doc.contains("extensions") || !doc["extensions"].is_array())
    return std::nullopt;
  MarketplaceIndex idx;
  for (const auto &item : doc["extensions"]) {
    if (!item.is_object()) continue;
    MarketplaceEntry e;
    e.id           = item.value("id", std::string{});
    e.name         = item.value("name", std::string{});
    e.version      = item.value("version", std::string{});
    e.publisher    = item.value("publisher", std::string{});
    e.download_url = item.value("download_url", std::string{});
    e.signature    = item.value("signature", std::string{});
    e.sha256       = item.value("sha256", std::string{});
    if (item.contains("capabilities") &&
        item["capabilities"].is_array()) {
      for (const auto &c : item["capabilities"])
        if (c.is_string())
          if (auto cap = CapabilityFromName(c.get<std::string>()))
            e.required_capabilities.push_back(*cap);
    }
    if (e.id.empty() || e.version.empty()) continue;
    idx.Add(std::move(e));
  }
  return idx;
}

void SignaturePolicy::Trust(const std::string &id,
                            const std::string &signature) {
  trusted_[id] = signature;
}

bool SignaturePolicy::Verify(const std::string &id,
                             const std::string &signature) const {
  if (!required_) return true;
  if (signature.empty()) return false;
  auto it = trusted_.find(id);
  return it != trusted_.end() && it->second == signature;
}

ExtensionManifest Marketplace::ManifestFromEntry(
    const MarketplaceEntry &e) {
  ExtensionManifest m;
  m.id = e.id;
  m.name = e.name.empty() ? e.id : e.name;
  m.version = e.version;
  m.publisher = e.publisher;
  m.entry_point = e.download_url.empty() ? e.id : e.download_url;
  m.required_capabilities = e.required_capabilities;
  return m;
}

InstallResult Marketplace::InstallEntry(const MarketplaceEntry &entry) {
  InstallResult r;
  if (!signing_.Verify(entry.id, entry.signature)) {
    r.message = "signature verification failed";
    return r;
  }
  if (auto prev = host_->Get(entry.id))
    r.previous_version = prev->manifest.version;

  bool ok = host_->Install(ManifestFromEntry(entry));
  if (!ok) {
    r.message = "install rejected (older or duplicate version)";
    return r;
  }
  history_[entry.id].push_back(entry.version);
  r.ok = true;
  r.message = "installed " + entry.id + "@" + entry.version;
  return r;
}

InstallResult Marketplace::Install(const std::string &id) {
  auto entry = index_.Find(id);
  if (!entry) {
    InstallResult r;
    r.message = "not in index";
    return r;
  }
  return InstallEntry(*entry);
}

InstallResult Marketplace::Install(const std::string &id,
                                   const std::string &version) {
  auto entry = index_.FindVersion(id, version);
  if (!entry) {
    InstallResult r;
    r.message = "version not in index";
    return r;
  }
  return InstallEntry(*entry);
}

InstallResult Marketplace::Update(const std::string &id) {
  auto entry = index_.Find(id);
  if (!entry) {
    InstallResult r;
    r.message = "not in index";
    return r;
  }
  auto prev = host_->Get(id);
  if (prev && CompareVersion(entry->version,
                             prev->manifest.version) <= 0) {
    InstallResult r;
    r.previous_version = prev->manifest.version;
    r.message = "already up to date";
    return r;
  }
  return InstallEntry(*entry);
}

InstallResult Marketplace::Rollback(const std::string &id) {
  auto it = history_.find(id);
  if (it == history_.end() || it->second.size() < 2) {
    InstallResult r;
    r.message = "no rollback target";
    return r;
  }
  std::string prev_version = it->second[it->second.size() - 2];
  auto entry = index_.FindVersion(id, prev_version);
  if (!entry) {
    InstallResult r;
    r.message = "previous version no longer indexed";
    return r;
  }
  // Remove the record so Install accepts the older version.
  host_->Uninstall(id);
  // Drop the most recent install from history before re-installing.
  it->second.pop_back();
  return InstallEntry(*entry);
}

bool Marketplace::Uninstall(const std::string &id) {
  history_.erase(id);
  return host_->Uninstall(id);
}

std::vector<std::string> Marketplace::History(const std::string &id) const {
  auto it = history_.find(id);
  if (it == history_.end()) return {};
  return it->second;
}

}  // namespace polyglot::tools::ui::ext
