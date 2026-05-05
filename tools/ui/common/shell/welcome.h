/**
 * @file     welcome.h
 * @brief    Welcome page value model: recent workspaces, tutorial
 *           and sample entries, what's-new tips.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::shell {

struct WelcomeWorkspaceEntry {
  std::string name;
  std::string path;
  long long last_opened_unix{0};
};

struct WelcomeTutorialEntry {
  std::string id;
  std::string title;
  std::string url;
};

struct WelcomeSampleEntry {
  std::string id;
  std::string title;
  std::string path;
};

struct WelcomeTipEntry {
  std::string id;
  std::string title;
  std::string body;
  std::string version;     ///< Version that introduced the tip.
};

class WelcomePage {
 public:
  void set_show_on_startup(bool v) { show_on_startup_ = v; }
  bool show_on_startup() const { return show_on_startup_; }
  void set_pinned(bool v) { pinned_ = v; }
  bool pinned() const { return pinned_; }

  void AddWorkspace(WelcomeWorkspaceEntry e);
  void AddTutorial(WelcomeTutorialEntry e);
  void AddSample(WelcomeSampleEntry e);
  void AddTip(WelcomeTipEntry e);

  const std::vector<WelcomeWorkspaceEntry> &workspaces() const {
    return workspaces_;
  }
  const std::vector<WelcomeTutorialEntry> &tutorials() const {
    return tutorials_;
  }
  const std::vector<WelcomeSampleEntry> &samples() const {
    return samples_;
  }
  /// Tips for `current_version` only (introduced at that version).
  std::vector<WelcomeTipEntry> TipsFor(const std::string &current_version) const;

  /// JSON snapshot used to persist the page across sessions.
  std::string Serialize() const;
  bool Load(const std::string &json);

 private:
  bool show_on_startup_{true};
  bool pinned_{false};
  std::vector<WelcomeWorkspaceEntry> workspaces_;
  std::vector<WelcomeTutorialEntry> tutorials_;
  std::vector<WelcomeSampleEntry> samples_;
  std::vector<WelcomeTipEntry> tips_;
};

}  // namespace polyglot::tools::ui::shell
