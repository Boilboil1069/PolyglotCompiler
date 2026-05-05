/**
 * @file     workspace.cpp
 * @brief    Implementation of the multi-root workspace model.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/workspace/workspace.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::workspace {

using Json = nlohmann::json;

namespace {

bool HasPrefix(const std::string &p, const std::string &prefix) {
  if (prefix.empty()) return false;
  if (prefix.size() > p.size()) return false;
  return p.compare(0, prefix.size(), prefix) == 0;
}

std::unordered_map<std::string, std::string> ReadStringMap(const Json &j) {
  std::unordered_map<std::string, std::string> out;
  if (!j.is_object()) return out;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (it.value().is_string()) out[it.key()] = it.value().get<std::string>();
    else if (it.value().is_number_integer())
      out[it.key()] = std::to_string(it.value().get<long long>());
    else if (it.value().is_boolean())
      out[it.key()] = it.value().get<bool>() ? "true" : "false";
  }
  return out;
}

}  // namespace

std::optional<WorkspaceFile> ParseWorkspaceFile(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return std::nullopt;
  }
  if (!doc.is_object()) return std::nullopt;
  if (!doc.contains("folders") || !doc["folders"].is_array())
    return std::nullopt;
  WorkspaceFile wf;
  for (const auto &f : doc["folders"]) {
    if (!f.is_object()) continue;
    WorkspaceFolder folder;
    folder.path = f.value("path", std::string{});
    folder.name = f.value("name", folder.path);
    if (folder.path.empty()) continue;
    if (f.contains("settings")) folder.settings = ReadStringMap(f["settings"]);
    wf.folders.push_back(std::move(folder));
  }
  if (doc.contains("settings")) wf.settings = ReadStringMap(doc["settings"]);
  return wf;
}

std::string SerializeWorkspaceFile(const WorkspaceFile &wf) {
  Json doc;
  Json folders = Json::array();
  for (const auto &f : wf.folders) {
    Json item;
    item["name"] = f.name;
    item["path"] = f.path;
    if (!f.settings.empty()) {
      Json s = Json::object();
      for (const auto &kv : f.settings) s[kv.first] = kv.second;
      item["settings"] = std::move(s);
    }
    folders.push_back(std::move(item));
  }
  doc["folders"] = std::move(folders);
  if (!wf.settings.empty()) {
    Json s = Json::object();
    for (const auto &kv : wf.settings) s[kv.first] = kv.second;
    doc["settings"] = std::move(s);
  }
  return doc.dump(2);
}

void Workspace::Load(WorkspaceFile wf) { file_ = std::move(wf); }

bool Workspace::AddRoot(WorkspaceFolder folder) {
  if (folder.name.empty() || folder.path.empty()) return false;
  for (const auto &f : file_.folders)
    if (f.name == folder.name) return false;
  file_.folders.push_back(std::move(folder));
  return true;
}

bool Workspace::RemoveRoot(const std::string &name) {
  auto it = std::find_if(file_.folders.begin(), file_.folders.end(),
                         [&](const auto &f) { return f.name == name; });
  if (it == file_.folders.end()) return false;
  file_.folders.erase(it);
  return true;
}

const WorkspaceFolder *Workspace::FindRoot(const std::string &name) const {
  for (const auto &f : file_.folders)
    if (f.name == name) return &f;
  return nullptr;
}

std::optional<std::string> Workspace::EffectiveSetting(
    const std::string &folder_name, const std::string &key) const {
  if (auto *f = FindRoot(folder_name)) {
    auto it = f->settings.find(key);
    if (it != f->settings.end()) return it->second;
  }
  auto it = file_.settings.find(key);
  if (it != file_.settings.end()) return it->second;
  return std::nullopt;
}

bool Workspace::ContainsPath(const std::string &path,
                             std::string *matched_root) const {
  for (const auto &f : file_.folders) {
    if (HasPrefix(path, f.path)) {
      if (matched_root) *matched_root = f.name;
      return true;
    }
  }
  return false;
}

std::vector<Workspace::SearchHit> Workspace::Search(
    const std::vector<std::pair<std::string, std::string>> &index,
    const std::string &query) const {
  std::vector<SearchHit> out;
  if (query.empty()) return out;
  for (const auto &kv : index) {
    if (kv.second.find(query) == std::string::npos) continue;
    std::string root;
    if (!ContainsPath(kv.first, &root)) continue;
    out.push_back({root, kv.first});
  }
  return out;
}

std::string LanguageServerPool::Acquire(const LanguageServerKey &key) {
  auto it = instances_.find(key);
  if (it != instances_.end()) return it->second;
  std::string id = "ls-" + std::to_string(++next_id_);
  instances_[key] = id;
  return id;
}

bool LanguageServerPool::Release(const LanguageServerKey &key) {
  return instances_.erase(key) > 0;
}

std::vector<LanguageServerKey> LanguageServerPool::keys() const {
  std::vector<LanguageServerKey> out;
  out.reserve(instances_.size());
  for (const auto &kv : instances_) out.push_back(kv.first);
  return out;
}

}  // namespace polyglot::tools::ui::workspace
