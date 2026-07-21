/**
 * @file     bookmarks.h
 * @brief    Cross-file bookmarks with labels and colours.
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

struct Bookmark {
  long long id{0};
  std::string path;
  long long line{0};
  std::string label;
  std::string color;     ///< "#RRGGBB" or palette name.
};

class BookmarkStore {
 public:
  /// Toggle a bookmark at `(path, line)`.  Returns the (possibly
  /// new) bookmark when added, nullopt when an existing entry was
  /// removed.
  std::optional<Bookmark> Toggle(const std::string &path, long long line,
                                 const std::string &label = {},
                                 const std::string &color = {});
  bool Remove(long long id);
  bool Relabel(long long id, const std::string &label);
  bool Recolor(long long id, const std::string &color);

  std::vector<Bookmark> All() const;
  std::vector<Bookmark> InFile(const std::string &path) const;
  std::optional<Bookmark> AtLine(const std::string &path,
                                 long long line) const;

  std::string Serialize() const;
  bool Load(const std::string &json);

 private:
  long long next_id_{0};
  std::vector<Bookmark> items_;
};

}  // namespace polyglot::tools::ui::shell
