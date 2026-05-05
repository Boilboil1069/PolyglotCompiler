/**
 * @file     bookmarks.cpp
 * @brief    Bookmark store implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/shell/bookmarks.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::shell {

using Json = nlohmann::json;

std::optional<Bookmark> BookmarkStore::Toggle(
    const std::string &path, long long line, const std::string &label,
    const std::string &color) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&](const auto &b) {
                           return b.path == path && b.line == line;
                         });
  if (it != items_.end()) {
    items_.erase(it);
    return std::nullopt;
  }
  Bookmark b;
  b.id = ++next_id_;
  b.path = path;
  b.line = line;
  b.label = label;
  b.color = color;
  items_.push_back(b);
  return b;
}

bool BookmarkStore::Remove(long long id) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&](const auto &b) { return b.id == id; });
  if (it == items_.end()) return false;
  items_.erase(it);
  return true;
}

bool BookmarkStore::Relabel(long long id, const std::string &label) {
  for (auto &b : items_)
    if (b.id == id) { b.label = label; return true; }
  return false;
}

bool BookmarkStore::Recolor(long long id, const std::string &color) {
  for (auto &b : items_)
    if (b.id == id) { b.color = color; return true; }
  return false;
}

std::vector<Bookmark> BookmarkStore::All() const {
  std::vector<Bookmark> out = items_;
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) {
              if (a.path != b.path) return a.path < b.path;
              return a.line < b.line;
            });
  return out;
}

std::vector<Bookmark> BookmarkStore::InFile(const std::string &path) const {
  std::vector<Bookmark> out;
  for (const auto &b : items_)
    if (b.path == path) out.push_back(b);
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) { return a.line < b.line; });
  return out;
}

std::optional<Bookmark> BookmarkStore::AtLine(const std::string &path,
                                              long long line) const {
  for (const auto &b : items_)
    if (b.path == path && b.line == line) return b;
  return std::nullopt;
}

std::string BookmarkStore::Serialize() const {
  Json arr = Json::array();
  for (const auto &b : items_)
    arr.push_back({{"id", b.id}, {"path", b.path}, {"line", b.line},
                   {"label", b.label}, {"color", b.color}});
  Json doc;
  doc["next_id"] = next_id_;
  doc["items"] = std::move(arr);
  return doc.dump(2);
}

bool BookmarkStore::Load(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return false;
  }
  if (!doc.is_object()) return false;
  next_id_ = doc.value("next_id", 0LL);
  items_.clear();
  if (doc.contains("items") && doc["items"].is_array()) {
    for (const auto &b : doc["items"]) {
      Bookmark x;
      x.id = b.value("id", 0LL);
      x.path = b.value("path", std::string{});
      x.line = b.value("line", 0LL);
      x.label = b.value("label", std::string{});
      x.color = b.value("color", std::string{});
      if (!x.path.empty()) items_.push_back(std::move(x));
    }
  }
  return true;
}

}  // namespace polyglot::tools::ui::shell
