/**
 * @file     recent.cpp
 * @brief    Recent list implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/shell/recent.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::shell {

using Json = nlohmann::json;

void RecentList::Touch(const RecentEntry &e) {
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [&](const auto &x) { return x.path == e.path; }),
               items_.end());
  items_.insert(items_.begin(), e);
  Trim();
}

bool RecentList::Pin(const std::string &path, bool pinned) {
  for (auto &x : items_)
    if (x.path == path) { x.pinned = pinned; return true; }
  return false;
}

bool RecentList::Remove(const std::string &path) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&](const auto &x) { return x.path == path; });
  if (it == items_.end()) return false;
  items_.erase(it);
  return true;
}

void RecentList::Clear() { items_.clear(); }

std::vector<RecentEntry> RecentList::Items() const {
  std::vector<RecentEntry> out = items_;
  std::stable_sort(out.begin(), out.end(),
                   [](const auto &a, const auto &b) {
                     if (a.pinned != b.pinned) return a.pinned;
                     return false;  // preserve insertion (recency) order
                   });
  return out;
}

std::optional<RecentEntry> RecentList::Find(const std::string &path) const {
  for (const auto &x : items_)
    if (x.path == path) return x;
  return std::nullopt;
}

void RecentList::Trim() {
  // Keep all pinned + most-recent unpinned, capped at capacity_.
  if (items_.size() <= capacity_) return;
  std::vector<RecentEntry> kept;
  kept.reserve(capacity_);
  for (const auto &x : items_)
    if (x.pinned) kept.push_back(x);
  for (const auto &x : items_) {
    if (kept.size() >= capacity_) break;
    if (!x.pinned) kept.push_back(x);
  }
  items_ = std::move(kept);
}

std::string RecentList::Serialize() const {
  Json arr = Json::array();
  for (const auto &e : items_)
    arr.push_back({{"path", e.path},
                   {"label", e.label},
                   {"last_opened_unix", e.last_opened_unix},
                   {"pinned", e.pinned}});
  Json doc;
  doc["capacity"] = static_cast<long long>(capacity_);
  doc["items"] = std::move(arr);
  return doc.dump(2);
}

bool RecentList::Load(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return false;
  }
  if (!doc.is_object()) return false;
  capacity_ = static_cast<size_t>(doc.value("capacity", 32LL));
  items_.clear();
  if (doc.contains("items") && doc["items"].is_array()) {
    for (const auto &e : doc["items"]) {
      RecentEntry x;
      x.path = e.value("path", std::string{});
      x.label = e.value("label", std::string{});
      x.last_opened_unix = e.value("last_opened_unix", 0LL);
      x.pinned = e.value("pinned", false);
      if (!x.path.empty()) items_.push_back(std::move(x));
    }
  }
  return true;
}

}  // namespace polyglot::tools::ui::shell
