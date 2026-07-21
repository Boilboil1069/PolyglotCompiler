/**
 * @file     notifications.cpp
 * @brief    Notification center implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/shell/notifications.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::shell {

using Json = nlohmann::json;

std::string NotificationSeverityName(NotificationSeverity s) {
  switch (s) {
    case NotificationSeverity::kInfo:     return "info";
    case NotificationSeverity::kWarning:  return "warning";
    case NotificationSeverity::kError:    return "error";
    case NotificationSeverity::kProgress: return "progress";
  }
  return "info";
}

namespace {

bool DnDSuppresses(NotificationSeverity s) {
  return s == NotificationSeverity::kInfo ||
         s == NotificationSeverity::kProgress;
}

}  // namespace

long long NotificationCenter::Post(Notification n) {
  if (dnd_ && DnDSuppresses(n.severity)) return 0;
  n.id = ++next_id_;
  if (n.created_unix == 0) n.created_unix = n.id;  // monotonic stand-in
  items_.push_back(std::move(n));
  return next_id_;
}

bool NotificationCenter::MarkRead(long long id) {
  for (auto &n : items_)
    if (n.id == id) { n.read = true; return true; }
  return false;
}

bool NotificationCenter::Dismiss(long long id) {
  for (auto &n : items_)
    if (n.id == id) { n.dismissed = true; n.read = true; return true; }
  return false;
}

void NotificationCenter::DismissAll() {
  for (auto &n : items_) { n.dismissed = true; n.read = true; }
}

size_t NotificationCenter::UnreadCount() const {
  size_t c = 0;
  for (const auto &n : items_)
    if (!n.read && !n.dismissed) ++c;
  return c;
}

std::vector<Notification> NotificationCenter::List(
    bool include_dismissed) const {
  std::vector<Notification> out;
  for (const auto &n : items_)
    if (include_dismissed || !n.dismissed) out.push_back(n);
  return out;
}

std::optional<Notification> NotificationCenter::Get(long long id) const {
  for (const auto &n : items_)
    if (n.id == id) return n;
  return std::nullopt;
}

std::string NotificationCenter::Serialize() const {
  Json doc;
  doc["next_id"] = next_id_;
  doc["dnd"] = dnd_;
  Json arr = Json::array();
  for (const auto &n : items_) {
    Json a = Json::array();
    for (const auto &act : n.actions)
      a.push_back({{"id", act.id}, {"label", act.label}});
    arr.push_back({{"id", n.id},
                   {"severity", NotificationSeverityName(n.severity)},
                   {"title", n.title},
                   {"body", n.body},
                   {"source", n.source},
                   {"created_unix", n.created_unix},
                   {"read", n.read},
                   {"dismissed", n.dismissed},
                   {"actions", a}});
  }
  doc["items"] = std::move(arr);
  return doc.dump(2);
}

bool NotificationCenter::Load(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return false;
  }
  if (!doc.is_object()) return false;
  next_id_ = doc.value("next_id", 0LL);
  dnd_ = doc.value("dnd", false);
  items_.clear();
  if (doc.contains("items") && doc["items"].is_array()) {
    for (const auto &n : doc["items"]) {
      Notification x;
      x.id = n.value("id", 0LL);
      auto sev = n.value("severity", std::string{"info"});
      if (sev == "warning") x.severity = NotificationSeverity::kWarning;
      else if (sev == "error") x.severity = NotificationSeverity::kError;
      else if (sev == "progress") x.severity = NotificationSeverity::kProgress;
      else x.severity = NotificationSeverity::kInfo;
      x.title = n.value("title", std::string{});
      x.body = n.value("body", std::string{});
      x.source = n.value("source", std::string{});
      x.created_unix = n.value("created_unix", 0LL);
      x.read = n.value("read", false);
      x.dismissed = n.value("dismissed", false);
      if (n.contains("actions") && n["actions"].is_array()) {
        for (const auto &a : n["actions"]) {
          NotificationAction act;
          act.id = a.value("id", std::string{});
          act.label = a.value("label", std::string{});
          x.actions.push_back(std::move(act));
        }
      }
      items_.push_back(std::move(x));
    }
  }
  return true;
}

}  // namespace polyglot::tools::ui::shell
