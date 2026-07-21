/**
 * @file     welcome.cpp
 * @brief    Welcome page value model implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/shell/welcome.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace polyglot::tools::ui::shell {

using Json = nlohmann::json;

void WelcomePage::AddWorkspace(WelcomeWorkspaceEntry e) {
  // Newest first; dedupe by path.
  workspaces_.erase(
      std::remove_if(workspaces_.begin(), workspaces_.end(),
                     [&](const auto &w) { return w.path == e.path; }),
      workspaces_.end());
  workspaces_.insert(workspaces_.begin(), std::move(e));
}

void WelcomePage::AddTutorial(WelcomeTutorialEntry e) {
  tutorials_.push_back(std::move(e));
}

void WelcomePage::AddSample(WelcomeSampleEntry e) {
  samples_.push_back(std::move(e));
}

void WelcomePage::AddTip(WelcomeTipEntry e) {
  tips_.push_back(std::move(e));
}

std::vector<WelcomeTipEntry> WelcomePage::TipsFor(
    const std::string &current_version) const {
  std::vector<WelcomeTipEntry> out;
  for (const auto &t : tips_)
    if (t.version == current_version) out.push_back(t);
  return out;
}

std::string WelcomePage::Serialize() const {
  Json doc;
  doc["show_on_startup"] = show_on_startup_;
  doc["pinned"] = pinned_;
  Json ws = Json::array();
  for (const auto &w : workspaces_) {
    ws.push_back({{"name", w.name}, {"path", w.path},
                  {"last_opened_unix", w.last_opened_unix}});
  }
  doc["workspaces"] = std::move(ws);
  Json tu = Json::array();
  for (const auto &t : tutorials_)
    tu.push_back({{"id", t.id}, {"title", t.title}, {"url", t.url}});
  doc["tutorials"] = std::move(tu);
  Json sa = Json::array();
  for (const auto &s : samples_)
    sa.push_back({{"id", s.id}, {"title", s.title}, {"path", s.path}});
  doc["samples"] = std::move(sa);
  Json ti = Json::array();
  for (const auto &t : tips_)
    ti.push_back({{"id", t.id}, {"title", t.title},
                  {"body", t.body}, {"version", t.version}});
  doc["tips"] = std::move(ti);
  return doc.dump(2);
}

bool WelcomePage::Load(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return false;
  }
  if (!doc.is_object()) return false;
  show_on_startup_ = doc.value("show_on_startup", true);
  pinned_ = doc.value("pinned", false);
  workspaces_.clear();
  tutorials_.clear();
  samples_.clear();
  tips_.clear();
  if (doc.contains("workspaces") && doc["workspaces"].is_array()) {
    for (const auto &w : doc["workspaces"]) {
      WelcomeWorkspaceEntry e;
      e.name = w.value("name", std::string{});
      e.path = w.value("path", std::string{});
      e.last_opened_unix = w.value("last_opened_unix", 0LL);
      workspaces_.push_back(std::move(e));
    }
  }
  if (doc.contains("tutorials") && doc["tutorials"].is_array()) {
    for (const auto &t : doc["tutorials"]) {
      WelcomeTutorialEntry e;
      e.id = t.value("id", std::string{});
      e.title = t.value("title", std::string{});
      e.url = t.value("url", std::string{});
      tutorials_.push_back(std::move(e));
    }
  }
  if (doc.contains("samples") && doc["samples"].is_array()) {
    for (const auto &s : doc["samples"]) {
      WelcomeSampleEntry e;
      e.id = s.value("id", std::string{});
      e.title = s.value("title", std::string{});
      e.path = s.value("path", std::string{});
      samples_.push_back(std::move(e));
    }
  }
  if (doc.contains("tips") && doc["tips"].is_array()) {
    for (const auto &t : doc["tips"]) {
      WelcomeTipEntry e;
      e.id = t.value("id", std::string{});
      e.title = t.value("title", std::string{});
      e.body = t.value("body", std::string{});
      e.version = t.value("version", std::string{});
      tips_.push_back(std::move(e));
    }
  }
  return true;
}

}  // namespace polyglot::tools::ui::shell
