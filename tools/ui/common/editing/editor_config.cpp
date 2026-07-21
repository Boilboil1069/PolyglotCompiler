/**
 * @file     editor_config.cpp
 * @brief    Implementation of the EditorConfig parser.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/editing/editor_config.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace polyglot::tools::ui {

namespace {

std::string Trim(std::string s) {
  std::size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  std::size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string LowerCopy(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::optional<bool> ParseBool(const std::string &v) {
  std::string lo = LowerCopy(v);
  if (lo == "true") return true;
  if (lo == "false") return false;
  return std::nullopt;
}

// Recursive glob matcher with `*`, `?`, `**`, `[set]`, `{a,b,c}`
// support.  This matches the EditorConfig spec subset that real
// projects use in practice.
bool MatchGlob(std::string_view pat, std::string_view txt) {
  std::size_t pi = 0;
  std::size_t ti = 0;
  while (pi < pat.size()) {
    char p = pat[pi];
    if (p == '\\' && pi + 1 < pat.size()) {
      if (ti < txt.size() && pat[pi + 1] == txt[ti]) { pi += 2; ++ti; continue; }
      return false;
    }
    if (p == '*') {
      bool ds = (pi + 1 < pat.size() && pat[pi + 1] == '*');
      std::size_t next_pi = pi + (ds ? 2 : 1);
      // Try every possible consumption of `txt` from `ti` onwards.
      for (std::size_t k = ti; k <= txt.size(); ++k) {
        if (!ds) {
          // Single `*` cannot cross a `/` separator.
          if (k > ti && txt[k - 1] == '/') break;
        }
        if (MatchGlob(pat.substr(next_pi), txt.substr(k))) return true;
      }
      return false;
    }
    if (p == '?') {
      if (ti >= txt.size() || txt[ti] == '/') return false;
      ++pi; ++ti; continue;
    }
    if (p == '[') {
      std::size_t end = pat.find(']', pi + 1);
      if (end == std::string_view::npos || ti >= txt.size()) return false;
      bool neg = pat[pi + 1] == '!';
      std::size_t k = pi + 1 + (neg ? 1 : 0);
      bool match = false;
      while (k < end) {
        if (k + 2 < end && pat[k + 1] == '-') {
          if (txt[ti] >= pat[k] && txt[ti] <= pat[k + 2]) match = true;
          k += 3;
        } else {
          if (pat[k] == txt[ti]) match = true;
          ++k;
        }
      }
      if (match == neg) return false;
      pi = end + 1; ++ti; continue;
    }
    if (p == '{') {
      std::size_t end = pat.find('}', pi + 1);
      if (end == std::string_view::npos) return false;
      std::string_view set = pat.substr(pi + 1, end - pi - 1);
      std::size_t start = 0;
      while (start <= set.size()) {
        std::size_t comma = set.find(',', start);
        std::string_view alt = set.substr(
            start, comma == std::string_view::npos ? set.size() - start
                                                   : comma - start);
        std::string sub_pat;
        sub_pat.append(alt);
        sub_pat.append(pat.substr(end + 1));
        if (MatchGlob(sub_pat, txt.substr(ti))) return true;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
      }
      return false;
    }
    if (ti < txt.size() && p == txt[ti]) { ++pi; ++ti; continue; }
    return false;
  }
  return ti == txt.size();
}

}  // namespace

bool EditorConfigMatch(std::string_view pattern, std::string_view path) {
  // EditorConfig: a pattern without a `/` matches against the basename.
  bool anchored = pattern.find('/') != std::string_view::npos;
  std::string_view text = path;
  if (!anchored) {
    std::size_t slash = path.rfind('/');
    if (slash != std::string_view::npos) text = path.substr(slash + 1);
  } else if (!pattern.empty() && pattern.front() == '/') {
    pattern.remove_prefix(1);
  }
  return MatchGlob(pattern, text);
}

void EditorConfigSettings::MergeFrom(const EditorConfigSettings &o) {
  if (o.indent_style) indent_style = o.indent_style;
  if (o.indent_size) indent_size = o.indent_size;
  if (o.end_of_line) end_of_line = o.end_of_line;
  if (o.charset) charset = o.charset;
  if (o.insert_final_newline) insert_final_newline = o.insert_final_newline;
  if (o.trim_trailing_whitespace)
    trim_trailing_whitespace = o.trim_trailing_whitespace;
}

std::string EditorConfigSettings::ToStatusString() const {
  std::ostringstream os;
  bool first = true;
  auto sep = [&]() { if (!first) os << " · "; first = false; };
  if (indent_style) {
    sep();
    os << *indent_style;
    if (indent_size) os << " " << *indent_size;
  }
  if (end_of_line) { sep(); os << "EOL=" << *end_of_line; }
  if (charset) { sep(); os << *charset; }
  if (insert_final_newline.value_or(false)) { sep(); os << "final-LF"; }
  if (trim_trailing_whitespace.value_or(false)) { sep(); os << "trim-WS"; }
  return os.str();
}

bool EditorConfigDocument::Parse(std::string_view text) {
  bool any = false;
  Section *current = nullptr;
  std::istringstream in{std::string(text)};
  std::string raw;
  while (std::getline(in, raw)) {
    std::string line = Trim(raw);
    if (line.empty() || line.front() == '#' || line.front() == ';') continue;
    if (line.front() == '[' && line.back() == ']') {
      sections_.push_back({line.substr(1, line.size() - 2), {}});
      current = &sections_.back();
      any = true;
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = LowerCopy(Trim(line.substr(0, eq)));
    std::string val = Trim(line.substr(eq + 1));
    if (current == nullptr) {
      if (key == "root") {
        auto b = ParseBool(val);
        if (b) root_ = *b;
      }
      continue;
    }
    EditorConfigSettings &s = current->settings;
    if (key == "indent_style") s.indent_style = LowerCopy(val);
    else if (key == "indent_size") {
      try { s.indent_size = static_cast<std::uint32_t>(std::stoul(val)); }
      catch (...) {}
    } else if (key == "end_of_line") s.end_of_line = LowerCopy(val);
    else if (key == "charset") s.charset = LowerCopy(val);
    else if (key == "insert_final_newline") s.insert_final_newline = ParseBool(val);
    else if (key == "trim_trailing_whitespace")
      s.trim_trailing_whitespace = ParseBool(val);
    any = true;
  }
  return any;
}

EditorConfigSettings EditorConfigDocument::ResolveFor(
    std::string_view relative_path) const {
  EditorConfigSettings out;
  for (const auto &sec : sections_) {
    if (EditorConfigMatch(sec.pattern, relative_path)) out.MergeFrom(sec.settings);
  }
  return out;
}

}  // namespace polyglot::tools::ui
