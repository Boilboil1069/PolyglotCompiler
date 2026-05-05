/**
 * @file     global_search_engine.cpp
 * @brief    Implementation of the workspace search engine
 *           (demand 2026-04-28-25 §4).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/search/global_search_engine.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace polyglot::tools::ui {

namespace {

bool GlobMatchOne(std::string_view pattern, std::string_view text) {
  // Mini glob: '*' matches any run (including '/'), '?' matches one
  // character, '**' is treated identically to '*' for our purposes.
  std::size_t pi = 0;
  std::size_t ti = 0;
  std::size_t star_p = std::string_view::npos;
  std::size_t star_t = 0;
  while (ti < text.size()) {
    if (pi < pattern.size() && (pattern[pi] == '?' ||
                                 pattern[pi] == text[ti])) {
      ++pi;
      ++ti;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      star_p = pi++;
      // Collapse '**' to '*'.
      while (pi < pattern.size() && pattern[pi] == '*') ++pi;
      star_t = ti;
    } else if (star_p != std::string_view::npos) {
      pi = star_p + 1;
      ti = ++star_t;
    } else {
      return false;
    }
  }
  while (pi < pattern.size() && pattern[pi] == '*') ++pi;
  return pi == pattern.size();
}

std::string EscapeRegex(std::string_view literal) {
  std::string out;
  out.reserve(literal.size() * 2);
  for (char c : literal) {
    switch (c) {
      case '.': case '\\': case '+': case '*': case '?':
      case '(': case ')': case '|': case '[': case ']':
      case '{': case '}': case '^': case '$':
        out.push_back('\\');
        [[fallthrough]];
      default:
        out.push_back(c);
    }
  }
  return out;
}

std::regex BuildRegex(const GlobalSearchOptions &opts) {
  std::string body =
      opts.regex ? opts.pattern : EscapeRegex(opts.pattern);
  if (opts.whole_word) body = "\\b" + body + "\\b";
  auto flags = std::regex::ECMAScript;
  if (!opts.case_sensitive) flags |= std::regex::icase;
  return std::regex(body, flags);
}

}  // namespace

bool MatchesGlobFilter(std::string_view path,
                       const std::vector<std::string> &include,
                       const std::vector<std::string> &exclude) {
  for (const auto &g : exclude) {
    if (GlobMatchOne(g, path)) return false;
  }
  if (include.empty()) return true;
  for (const auto &g : include) {
    if (GlobMatchOne(g, path)) return true;
  }
  return false;
}

std::size_t SearchInBuffer(const std::string &path,
                           std::string_view contents,
                           const GlobalSearchOptions &opts,
                           const GlobalSearchSink &sink) {
  if (opts.pattern.empty()) return 0;
  std::regex re;
  try {
    re = BuildRegex(opts);
  } catch (const std::regex_error &) {
    return 0;
  }
  std::size_t count = 0;
  std::uint32_t line_no = 0;
  std::size_t line_start = 0;
  for (std::size_t i = 0; i <= contents.size(); ++i) {
    const bool eol = (i == contents.size()) || contents[i] == '\n';
    if (!eol) continue;
    const std::string_view line(contents.data() + line_start,
                                i - line_start);
    auto begin = std::cregex_iterator(line.data(),
                                      line.data() + line.size(), re);
    auto end = std::cregex_iterator();
    for (auto it = begin; it != end; ++it) {
      GlobalSearchHit h;
      h.path = path;
      h.line = line_no;
      h.column = static_cast<std::uint32_t>(it->position(0));
      h.match_len = static_cast<std::uint32_t>(it->length(0));
      h.line_text.assign(line.data(), line.size());
      ++count;
      if (!sink(h)) return count;
      if (opts.max_results > 0 && count >= opts.max_results) return count;
    }
    ++line_no;
    line_start = i + 1;
  }
  return count;
}

std::string ReplaceInBuffer(std::string_view contents,
                            const GlobalSearchOptions &opts,
                            std::string_view replacement,
                            std::size_t *out_count) {
  if (opts.pattern.empty()) return std::string(contents);
  std::regex re;
  try {
    re = BuildRegex(opts);
  } catch (const std::regex_error &) {
    if (out_count) *out_count = 0;
    return std::string(contents);
  }
  std::size_t count = 0;
  if (out_count) {
    auto begin = std::cregex_iterator(
        contents.data(), contents.data() + contents.size(), re);
    auto end = std::cregex_iterator();
    for (auto it = begin; it != end; ++it) ++count;
    *out_count = count;
  }
  return std::regex_replace(std::string(contents), re,
                            std::string(replacement));
}

}  // namespace polyglot::tools::ui
