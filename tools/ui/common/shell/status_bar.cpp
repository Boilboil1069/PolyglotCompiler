/**
 * @file     status_bar.cpp
 * @brief    Status bar value model implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/shell/status_bar.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::shell {

using Json = nlohmann::json;

std::string StatusAlignmentName(StatusAlignment a) {
  return a == StatusAlignment::kLeft ? "left" : "right";
}

namespace {

StatusAlignment AlignmentFromName(const std::string &n) {
  return n == "right" ? StatusAlignment::kRight : StatusAlignment::kLeft;
}

}  // namespace

void StatusBar::RegisterBuiltins() {
  static const struct {
    const char *id;
    const char *label;
    StatusAlignment a;
    int priority;
  } kBuiltins[] = {
      {"branch",         "main",        StatusAlignment::kLeft,  100},
      {"problems",       "0 ✗ 0 ⚠",     StatusAlignment::kLeft,   90},
      {"language",       "ploy",        StatusAlignment::kRight, 100},
      {"language_server","polyls: ok",  StatusAlignment::kRight,  90},
      {"encoding",       "UTF-8",       StatusAlignment::kRight,  80},
      {"eol",            "LF",          StatusAlignment::kRight,  70},
      {"indent",         "Spaces: 2",   StatusAlignment::kRight,  60},
      {"package_manager","polypkg",     StatusAlignment::kRight,  50},
      {"profiler",       "idle",        StatusAlignment::kRight,  40},
  };
  for (const auto &b : kBuiltins) {
    StatusBarItem it;
    it.id = b.id;
    it.label = b.label;
    it.alignment = b.a;
    it.priority = b.priority;
    it.owner = "builtin";
    Register(std::move(it));
  }
}

bool StatusBar::Register(StatusBarItem item) {
  if (item.id.empty()) return false;
  for (const auto &i : items_)
    if (i.id == item.id) return false;
  items_.push_back(std::move(item));
  return true;
}

bool StatusBar::Unregister(const std::string &id) {
  auto it = std::find_if(items_.begin(), items_.end(),
                         [&](const auto &x) { return x.id == id; });
  if (it == items_.end()) return false;
  items_.erase(it);
  return true;
}

bool StatusBar::SetVisible(const std::string &id, bool visible) {
  for (auto &i : items_)
    if (i.id == id) { i.visible = visible; return true; }
  return false;
}

bool StatusBar::Move(const std::string &id, StatusAlignment a, int p) {
  for (auto &i : items_)
    if (i.id == id) { i.alignment = a; i.priority = p; return true; }
  return false;
}

std::vector<StatusBarItem> StatusBar::Visible(
    StatusAlignment alignment) const {
  std::vector<StatusBarItem> out;
  for (const auto &i : items_)
    if (i.visible && i.alignment == alignment) out.push_back(i);
  std::sort(out.begin(), out.end(),
            [](const auto &x, const auto &y) {
              if (x.priority != y.priority) return x.priority > y.priority;
              return x.id < y.id;
            });
  return out;
}

std::optional<StatusBarItem> StatusBar::Find(const std::string &id) const {
  for (const auto &i : items_)
    if (i.id == id) return i;
  return std::nullopt;
}

std::string StatusBar::Serialize() const {
  Json arr = Json::array();
  for (const auto &i : items_) {
    arr.push_back({{"id", i.id},
                   {"label", i.label},
                   {"tooltip", i.tooltip},
                   {"alignment", StatusAlignmentName(i.alignment)},
                   {"priority", i.priority},
                   {"visible", i.visible},
                   {"owner", i.owner}});
  }
  Json doc;
  doc["items"] = std::move(arr);
  return doc.dump(2);
}

bool StatusBar::Load(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return false;
  }
  items_.clear();
  if (!doc.contains("items") || !doc["items"].is_array()) return false;
  for (const auto &i : doc["items"]) {
    StatusBarItem it;
    it.id = i.value("id", std::string{});
    it.label = i.value("label", std::string{});
    it.tooltip = i.value("tooltip", std::string{});
    it.alignment = AlignmentFromName(i.value("alignment",
                                             std::string{"left"}));
    it.priority = i.value("priority", 0);
    it.visible = i.value("visible", true);
    it.owner = i.value("owner", std::string{});
    if (!it.id.empty()) items_.push_back(std::move(it));
  }
  return true;
}

}  // namespace polyglot::tools::ui::shell
