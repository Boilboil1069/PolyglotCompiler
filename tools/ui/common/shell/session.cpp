/**
 * @file     session.cpp
 * @brief    Session serialisation implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/shell/session.h"

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::shell {

using Json = nlohmann::json;

std::string SplitOrientationName(SplitOrientation s) {
  return s == SplitOrientation::kVertical ? "vertical" : "horizontal";
}

namespace {

SplitOrientation OrientationFromName(const std::string &n) {
  return n == "vertical" ? SplitOrientation::kVertical
                         : SplitOrientation::kHorizontal;
}

Json TabToJson(const SessionTab &t) {
  Json folds = Json::array();
  for (const auto &f : t.folds)
    folds.push_back({f.first, f.second});
  return {{"path", t.path},
          {"cursor_line", t.cursor_line},
          {"cursor_column", t.cursor_column},
          {"scroll_top_line", t.scroll_top_line},
          {"folds", folds},
          {"active", t.active}};
}

SessionTab TabFromJson(const Json &j) {
  SessionTab t;
  t.path = j.value("path", std::string{});
  t.cursor_line = j.value("cursor_line", 0LL);
  t.cursor_column = j.value("cursor_column", 0LL);
  t.scroll_top_line = j.value("scroll_top_line", 0LL);
  t.active = j.value("active", false);
  if (j.contains("folds") && j["folds"].is_array()) {
    for (const auto &f : j["folds"]) {
      if (f.is_array() && f.size() == 2)
        t.folds.emplace_back(f[0].get<long long>(), f[1].get<long long>());
    }
  }
  return t;
}

}  // namespace

std::string SessionStore::Serialize(const Session &s) const {
  Json doc;
  doc["enabled"] = s.enabled;
  Json split;
  split["orientation"] = SplitOrientationName(s.split.orientation);
  Json panes = Json::array();
  for (const auto &p : s.split.panes) {
    Json tabs = Json::array();
    for (const auto &t : p.tabs) tabs.push_back(TabToJson(t));
    panes.push_back({{"id", p.id}, {"tabs", tabs}});
  }
  split["panes"] = std::move(panes);
  doc["split"] = std::move(split);

  doc["panels"] = {{"sidebar_width", s.panels.sidebar_width},
                   {"bottom_height", s.panels.bottom_height},
                   {"right_width", s.panels.right_width},
                   {"sidebar_visible", s.panels.sidebar_visible},
                   {"bottom_visible", s.panels.bottom_visible}};

  Json watches = Json::array();
  for (const auto &w : s.debug.watch_expressions) watches.push_back(w);
  Json views = Json::array();
  for (const auto &v : s.debug.open_views) views.push_back(v);
  doc["debug"] = {{"active", s.debug.active},
                  {"configuration", s.debug.configuration},
                  {"watch_expressions", watches},
                  {"open_views", views}};

  Json extras = Json::object();
  for (const auto &kv : s.extras) extras[kv.first] = kv.second;
  doc["extras"] = std::move(extras);
  return doc.dump(2);
}

std::optional<Session> SessionStore::Deserialize(
    const std::string &json) const {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return std::nullopt;
  }
  if (!doc.is_object()) return std::nullopt;
  Session s;
  s.enabled = doc.value("enabled", true);
  if (doc.contains("split") && doc["split"].is_object()) {
    const auto &sp = doc["split"];
    s.split.orientation =
        OrientationFromName(sp.value("orientation", std::string{"horizontal"}));
    if (sp.contains("panes") && sp["panes"].is_array()) {
      for (const auto &p : sp["panes"]) {
        SessionPane pane;
        pane.id = p.value("id", std::string{});
        if (p.contains("tabs") && p["tabs"].is_array())
          for (const auto &t : p["tabs"]) pane.tabs.push_back(TabFromJson(t));
        s.split.panes.push_back(std::move(pane));
      }
    }
  }
  if (doc.contains("panels") && doc["panels"].is_object()) {
    const auto &p = doc["panels"];
    s.panels.sidebar_width = p.value("sidebar_width", 240);
    s.panels.bottom_height = p.value("bottom_height", 200);
    s.panels.right_width = p.value("right_width", 0);
    s.panels.sidebar_visible = p.value("sidebar_visible", true);
    s.panels.bottom_visible = p.value("bottom_visible", true);
  }
  if (doc.contains("debug") && doc["debug"].is_object()) {
    const auto &d = doc["debug"];
    s.debug.active = d.value("active", false);
    s.debug.configuration = d.value("configuration", std::string{});
    if (d.contains("watch_expressions") && d["watch_expressions"].is_array())
      for (const auto &w : d["watch_expressions"])
        if (w.is_string()) s.debug.watch_expressions.push_back(w);
    if (d.contains("open_views") && d["open_views"].is_array())
      for (const auto &v : d["open_views"])
        if (v.is_string()) s.debug.open_views.push_back(v);
  }
  if (doc.contains("extras") && doc["extras"].is_object()) {
    for (auto it = doc["extras"].begin(); it != doc["extras"].end(); ++it)
      if (it.value().is_string())
        s.extras[it.key()] = it.value().get<std::string>();
  }
  return s;
}

}  // namespace polyglot::tools::ui::shell
