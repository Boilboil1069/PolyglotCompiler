/**
 * @file     accessibility.cpp
 * @brief    Accessibility value-model implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/a11y/accessibility.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::a11y {

using Json = nlohmann::json;

bool FocusOrder::Register(FocusableWidget w) {
  if (w.id.empty()) return false;
  for (const auto &x : widgets_)
    if (x.id == w.id) return false;
  widgets_.push_back(std::move(w));
  return true;
}

bool FocusOrder::Unregister(const std::string &id) {
  auto it = std::find_if(widgets_.begin(), widgets_.end(),
                         [&](const auto &x) { return x.id == id; });
  if (it == widgets_.end()) return false;
  widgets_.erase(it);
  return true;
}

bool FocusOrder::SetFocusable(const std::string &id, bool f) {
  for (auto &x : widgets_)
    if (x.id == id) { x.focusable = f; return true; }
  return false;
}

std::vector<FocusableWidget> FocusOrder::Order() const {
  std::vector<FocusableWidget> out = widgets_;
  std::stable_sort(out.begin(), out.end(),
                   [](const auto &a, const auto &b) {
                     if (a.tab_index != b.tab_index)
                       return a.tab_index < b.tab_index;
                     return a.id < b.id;
                   });
  return out;
}

namespace {

std::vector<FocusableWidget> Focusables(const FocusOrder &fo) {
  auto all = fo.Order();
  std::vector<FocusableWidget> out;
  for (auto &w : all) if (w.focusable) out.push_back(std::move(w));
  return out;
}

}  // namespace

std::optional<std::string> FocusOrder::Next(
    const std::string &current) const {
  auto list = Focusables(*this);
  if (list.empty()) return std::nullopt;
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].id == current) return list[(i + 1) % list.size()].id;
  }
  return list.front().id;
}

std::optional<std::string> FocusOrder::Previous(
    const std::string &current) const {
  auto list = Focusables(*this);
  if (list.empty()) return std::nullopt;
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].id == current)
      return list[(i + list.size() - 1) % list.size()].id;
  }
  return list.back().id;
}

std::string AnnouncementPriorityName(AnnouncementPriority p) {
  return p == AnnouncementPriority::kAssertive ? "assertive" : "polite";
}

void ScreenReaderQueue::Post(ScreenReaderAnnouncement a) {
  items_.push_back(std::move(a));
}

std::vector<ScreenReaderAnnouncement> ScreenReaderQueue::Drain() {
  std::vector<ScreenReaderAnnouncement> assertive;
  std::vector<ScreenReaderAnnouncement> polite;
  for (auto &a : items_) {
    if (a.priority == AnnouncementPriority::kAssertive)
      assertive.push_back(std::move(a));
    else
      polite.push_back(std::move(a));
  }
  items_.clear();
  std::vector<ScreenReaderAnnouncement> out;
  out.reserve(assertive.size() + polite.size());
  for (auto &a : assertive) out.push_back(std::move(a));
  for (auto &a : polite)    out.push_back(std::move(a));
  return out;
}

AccessibilityProfile NormalizeProfile(AccessibilityProfile p) {
  if (p.font_scale_percent < 80)  p.font_scale_percent = 80;
  if (p.font_scale_percent > 300) p.font_scale_percent = 300;
  return p;
}

std::string SerializeProfile(const AccessibilityProfile &p) {
  Json doc;
  doc["high_contrast"] = p.high_contrast;
  doc["large_font"] = p.large_font;
  doc["reduce_motion"] = p.reduce_motion;
  doc["font_scale_percent"] = p.font_scale_percent;
  doc["preferred_theme"] = p.preferred_theme;
  return doc.dump(2);
}

std::optional<AccessibilityProfile> DeserializeProfile(
    const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return std::nullopt;
  }
  if (!doc.is_object()) return std::nullopt;
  AccessibilityProfile p;
  p.high_contrast = doc.value("high_contrast", false);
  p.large_font = doc.value("large_font", false);
  p.reduce_motion = doc.value("reduce_motion", false);
  p.font_scale_percent = doc.value("font_scale_percent", 100);
  p.preferred_theme = doc.value("preferred_theme", std::string{});
  return NormalizeProfile(p);
}

}  // namespace polyglot::tools::ui::a11y
