/**
 * @file     session.h
 * @brief    Session restore: tabs / scroll / cursor / folds / split
 *           layout / panel sizes / debug view state.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::shell {

struct SessionTab {
  std::string path;
  long long cursor_line{0};
  long long cursor_column{0};
  long long scroll_top_line{0};
  std::vector<std::pair<long long, long long>> folds;  ///< (start,end)
  bool active{false};
};

enum class SplitOrientation {
  kHorizontal,
  kVertical,
};

std::string SplitOrientationName(SplitOrientation s);

struct SessionPane {
  std::string id;
  std::vector<SessionTab> tabs;
};

struct SessionSplit {
  SplitOrientation orientation{SplitOrientation::kHorizontal};
  std::vector<SessionPane> panes;
};

struct SessionPanelSizes {
  int sidebar_width{240};
  int bottom_height{200};
  int right_width{0};
  bool sidebar_visible{true};
  bool bottom_visible{true};
};

struct SessionDebugState {
  bool active{false};
  std::string configuration;
  std::vector<std::string> watch_expressions;
  std::vector<std::string> open_views;
};

struct Session {
  bool enabled{true};
  SessionSplit split;
  SessionPanelSizes panels;
  SessionDebugState debug;
  std::unordered_map<std::string, std::string> extras;
};

class SessionStore {
 public:
  std::string Serialize(const Session &s) const;
  std::optional<Session> Deserialize(const std::string &json) const;
};

}  // namespace polyglot::tools::ui::shell
