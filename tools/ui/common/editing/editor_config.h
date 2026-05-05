/**
 * @file     editor_config.h
 * @brief    Minimal `.editorconfig` parser + glob resolver
 *           (demand 2026-04-28-26 §5).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace polyglot::tools::ui {

struct EditorConfigSettings {
  std::optional<std::string> indent_style;        ///< "space" / "tab"
  std::optional<std::uint32_t> indent_size;
  std::optional<std::string> end_of_line;         ///< "lf" / "crlf" / "cr"
  std::optional<std::string> charset;             ///< "utf-8", …
  std::optional<bool> insert_final_newline;
  std::optional<bool> trim_trailing_whitespace;

  /// Renders a single-line summary suitable for the status bar.
  std::string ToStatusString() const;

  /// Merges `other` into `*this`, with `other` winning on conflicts.
  void MergeFrom(const EditorConfigSettings &other);
};

/// Parsed `.editorconfig` document.  Sections are stored in source
/// order so later sections override earlier ones — matching the
/// EditorConfig spec.
class EditorConfigDocument {
 public:
  /// Parses `text`.  Returns `true` if at least one section/key was
  /// recognised; malformed lines are skipped.
  bool Parse(std::string_view text);

  /// Returns `true` if `root = true` was set in the preamble.
  bool IsRoot() const { return root_; }

  /// Resolves the merged settings for `relative_path` (path relative
  /// to the directory containing this `.editorconfig`).
  EditorConfigSettings ResolveFor(std::string_view relative_path) const;

 private:
  struct Section {
    std::string pattern;
    EditorConfigSettings settings;
  };
  bool root_{false};
  std::vector<Section> sections_;
};

/// Returns `true` iff `path` matches the EditorConfig glob `pattern`
/// (`*`, `?`, `**`, `[abc]`, `{js,ts}` are supported).
bool EditorConfigMatch(std::string_view pattern, std::string_view path);

}  // namespace polyglot::tools::ui
