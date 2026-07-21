/**
 * @file     global_search_engine.h
 * @brief    Workspace-wide find-and-replace engine
 *           (demand 2026-04-28-25 §4).
 *
 * Pure-C++ engine that walks a directory tree, applies glob include /
 * exclude filters, and emits matches through a callback so the UI can
 * render results progressively without blocking.  Supports literal,
 * regex, case-sensitive and whole-word search modes; replacement
 * accepts capture-group references when regex mode is enabled.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace polyglot::tools::ui {

struct GlobalSearchOptions {
  std::string pattern;
  bool regex{false};
  bool case_sensitive{false};
  bool whole_word{false};
  std::vector<std::string> include_globs;  ///< empty = all files
  std::vector<std::string> exclude_globs;  ///< always evaluated
  std::size_t max_results{10000};          ///< 0 = unlimited
};

struct GlobalSearchHit {
  std::string path;
  std::uint32_t line{0};       ///< 0-based line number
  std::uint32_t column{0};     ///< 0-based byte column of match start
  std::uint32_t match_len{0};  ///< match length in bytes
  std::string line_text;       ///< full line for the result panel
};

using GlobalSearchSink = std::function<bool(const GlobalSearchHit &)>;

/// True iff `path` matches the include filter (or filter is empty)
/// AND does not match any exclude filter.  Glob syntax supported:
/// `*`, `?`, character classes are not supported; trailing `/**`
/// matches any descendant.
bool MatchesGlobFilter(std::string_view path,
                       const std::vector<std::string> &include,
                       const std::vector<std::string> &exclude);

/// Search the in-memory text @p contents of a single file.  Used by
/// tests and by the file-by-file driver below.  Returns the number of
/// hits emitted.  When @p sink returns `false` the search is aborted
/// early.
std::size_t SearchInBuffer(const std::string &path,
                           std::string_view contents,
                           const GlobalSearchOptions &opts,
                           const GlobalSearchSink &sink);

/// Replace every occurrence of `opts.pattern` inside @p contents with
/// @p replacement.  When `opts.regex` is true, `replacement` may use
/// `$1`-style back-references.  Returns the rewritten buffer; the
/// number of substitutions is reported through @p out_count when
/// non-null.
std::string ReplaceInBuffer(std::string_view contents,
                            const GlobalSearchOptions &opts,
                            std::string_view replacement,
                            std::size_t *out_count = nullptr);

}  // namespace polyglot::tools::ui
