/**
 * @file     status_bar.h
 * @brief    Customisable status bar: built-in slots, drag-to-
 *           reorder, show/hide, third-party registration.
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

enum class StatusAlignment {
  kLeft,
  kRight,
};

std::string StatusAlignmentName(StatusAlignment a);

struct StatusBarItem {
  std::string id;             ///< Unique slot id.
  std::string label;
  std::string tooltip;
  StatusAlignment alignment{StatusAlignment::kLeft};
  int priority{0};            ///< Higher = closer to the centre.
  bool visible{true};
  std::string owner;          ///< "builtin" or extension id.
};

class StatusBar {
 public:
  /// Seed the bar with the built-in slots required by the demand
  /// spec: branch, encoding, eol, indent, language, language
  /// server status, package manager, profiler, problems.
  void RegisterBuiltins();

  bool Register(StatusBarItem item);
  bool Unregister(const std::string &id);

  bool SetVisible(const std::string &id, bool visible);
  bool Move(const std::string &id, StatusAlignment alignment, int priority);

  const std::vector<StatusBarItem> &items() const { return items_; }
  std::vector<StatusBarItem> Visible(StatusAlignment alignment) const;
  std::optional<StatusBarItem> Find(const std::string &id) const;

  std::string Serialize() const;
  bool Load(const std::string &json);

 private:
  std::vector<StatusBarItem> items_;
};

}  // namespace polyglot::tools::ui::shell
