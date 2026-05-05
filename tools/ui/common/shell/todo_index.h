/**
 * @file     todo_index.h
 * @brief    Background TODO / FIXME / custom-keyword index with a
 *           summary view model.
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

struct TodoItem {
  std::string keyword;        ///< TODO / FIXME / XXX / HACK / ...
  std::string path;
  long long line{0};
  std::string text;           ///< Trimmed comment text after the keyword.
};

/// Index built from text + path pairs.  Keywords default to TODO
/// and FIXME; users can extend the set via `set_keywords`.
class TodoIndex {
 public:
  TodoIndex();
  void set_keywords(std::vector<std::string> keywords);
  const std::vector<std::string> &keywords() const { return keywords_; }

  /// Reindex `path` from `text`.  Replaces any existing entries
  /// for that path.  Returns the number of items found.
  size_t Scan(const std::string &path, const std::string &text);
  void Forget(const std::string &path);
  void Clear();

  std::vector<TodoItem> All() const;
  std::vector<TodoItem> InFile(const std::string &path) const;
  std::vector<TodoItem> ForKeyword(const std::string &keyword) const;
  std::unordered_map<std::string, size_t> CountsByKeyword() const;

 private:
  std::vector<std::string> keywords_;
  std::unordered_map<std::string, std::vector<TodoItem>> by_path_;
};

}  // namespace polyglot::tools::ui::shell
