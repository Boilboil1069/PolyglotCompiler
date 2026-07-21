/**
 * @file     snippet_engine.h
 * @brief    VS Code-flavoured snippet expansion
 *           (demand 2026-04-28-26 §4).
 *
 * Supports `$1` tabstops, `${1:default}` placeholders,
 * `${2|a,b,c|}` choice placeholders and a small set of variables
 * (`$CURRENT_DATE`, `$CURRENT_YEAR`, `$TM_FILENAME`, …).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace polyglot::tools::ui {

struct SnippetTabstop {
  std::uint32_t index{0};   ///< Tabstop number (`1` for `$1`); `0`
                            ///< marks the final caret position.
  std::uint32_t offset{0};  ///< Byte offset into the expanded text.
  std::uint32_t length{0};  ///< Length of the placeholder default.
  std::vector<std::string> choices;  ///< Empty unless a `|a,b|` set.
};

struct SnippetExpansion {
  std::string text;
  std::vector<SnippetTabstop> tabstops;  ///< Sorted by document order.
};

/// Expands `body` substituting variables from `vars`.  Unknown
/// variables resolve to the empty string.  Tabstop offsets refer to
/// `text` byte positions.
SnippetExpansion ExpandSnippet(std::string_view body,
                               const std::map<std::string, std::string> &vars);

struct SnippetEntry {
  std::string name;     ///< Human-readable name.
  std::string prefix;   ///< Trigger prefix (used by completion).
  std::string body;     ///< Snippet body with placeholders.
  std::string description;
};

/// Loads snippets from a permissive JSON-like representation:
///   `{ "name": { "prefix": "p", "body": "...", "description": "d" } }`
/// `body` may also be a JSON array of strings, joined by `\n`.
class SnippetLibrary {
 public:
  /// Inserts/overwrites a single entry.
  void Add(SnippetEntry entry);

  /// Loads entries from `json_text`.  Returns the number of entries
  /// successfully parsed; on a parse error the library is left
  /// untouched and `0` is returned.
  std::size_t LoadJson(std::string_view json_text);

  const std::vector<SnippetEntry> &Entries() const { return entries_; }

  /// Returns entries whose `prefix` starts with `needle` (case
  /// sensitive).  Suitable for feeding completion lists.
  std::vector<SnippetEntry> Match(std::string_view needle) const;

 private:
  std::vector<SnippetEntry> entries_;
};

}  // namespace polyglot::tools::ui
