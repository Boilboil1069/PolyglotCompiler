/**
 * @file     accessibility.h
 * @brief    Keyboard focus order, screen-reader announcements and
 *           visual accessibility profile (high contrast, large
 *           font, reduced motion).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::a11y {

struct FocusableWidget {
  std::string id;
  std::string role;          ///< "button" / "textbox" / "list" / ...
  std::string label;         ///< Accessible name.
  int tab_index{0};          ///< Lower wins; ties broken by id.
  bool focusable{true};
};

/// Linear keyboard tab order.  Always reachable; an entry with
/// `focusable=false` is skipped without breaking the chain.
class FocusOrder {
 public:
  bool Register(FocusableWidget w);
  bool Unregister(const std::string &id);
  bool SetFocusable(const std::string &id, bool focusable);

  std::vector<FocusableWidget> Order() const;
  /// Returns the next focusable id after `current` (or the first
  /// focusable id when `current` is empty / unknown).  Wraps
  /// around.  Returns nullopt only when no widget is focusable.
  std::optional<std::string> Next(const std::string &current) const;
  std::optional<std::string> Previous(const std::string &current) const;

 private:
  std::vector<FocusableWidget> widgets_;
};

enum class AnnouncementPriority {
  kPolite,    ///< Queued; spoken when the reader is idle.
  kAssertive, ///< Interrupts the reader (errors, alerts).
};

std::string AnnouncementPriorityName(AnnouncementPriority p);

struct ScreenReaderAnnouncement {
  std::string id;
  std::string text;
  AnnouncementPriority priority{AnnouncementPriority::kPolite};
};

/// Drains in priority order: assertive first (FIFO), polite next
/// (FIFO).  The Qt layer feeds drained items to the platform
/// accessibility bridge (UIA / AT-SPI / NSAccessibility).
class ScreenReaderQueue {
 public:
  void Post(ScreenReaderAnnouncement a);
  size_t size() const { return items_.size(); }
  std::vector<ScreenReaderAnnouncement> Drain();

 private:
  std::vector<ScreenReaderAnnouncement> items_;
};

struct AccessibilityProfile {
  bool high_contrast{false};
  bool large_font{false};
  bool reduce_motion{false};
  int font_scale_percent{100};   ///< 100 = default; clamped to 80..300.
  std::string preferred_theme;   ///< Optional override.
};

std::string SerializeProfile(const AccessibilityProfile &p);
std::optional<AccessibilityProfile> DeserializeProfile(
    const std::string &json);
AccessibilityProfile NormalizeProfile(AccessibilityProfile p);

}  // namespace polyglot::tools::ui::a11y
