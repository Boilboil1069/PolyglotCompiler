/**
 * @file     notifications.h
 * @brief    Persistent notification center with severity, action
 *           buttons, do-not-disturb mode and unread count.
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

enum class NotificationSeverity {
  kInfo,
  kWarning,
  kError,
  kProgress,
};

std::string NotificationSeverityName(NotificationSeverity s);

struct NotificationAction {
  std::string id;
  std::string label;
};

struct Notification {
  long long id{0};
  NotificationSeverity severity{NotificationSeverity::kInfo};
  std::string title;
  std::string body;
  std::string source;            ///< Subsystem that posted it.
  long long created_unix{0};
  bool read{false};
  bool dismissed{false};
  std::vector<NotificationAction> actions;
};

class NotificationCenter {
 public:
  long long Post(Notification n);
  bool MarkRead(long long id);
  bool Dismiss(long long id);
  void DismissAll();

  size_t UnreadCount() const;
  std::vector<Notification> List(bool include_dismissed = false) const;
  std::optional<Notification> Get(long long id) const;

  /// Do-not-disturb suppresses `kInfo` and `kProgress` posts.
  /// Errors and warnings always go through (per the demand spec
  /// "不打扰模式" applies to non-critical levels).
  void set_do_not_disturb(bool v) { dnd_ = v; }
  bool do_not_disturb() const { return dnd_; }

  std::string Serialize() const;
  bool Load(const std::string &json);

 private:
  long long next_id_{0};
  bool dnd_{false};
  std::vector<Notification> items_;
};

}  // namespace polyglot::tools::ui::shell
