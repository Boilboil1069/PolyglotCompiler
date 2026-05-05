/**
 * @file     format_engine.cpp
 * @brief    Implementation of `FormatPloy`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/editing/format_engine.h"

#include <cctype>
#include <sstream>
#include <vector>

namespace polyglot::tools::ui {

namespace {

std::string_view Trim(std::string_view s) {
  std::size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  std::size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string MakeIndent(std::uint32_t depth, const FormatOptions &opts) {
  if (opts.insert_spaces) {
    return std::string(static_cast<std::size_t>(depth) * opts.tab_size, ' ');
  }
  return std::string(depth, '\t');
}

}  // namespace

std::string FormatPloy(std::string_view text, const FormatOptions &opts) {
  std::vector<std::string_view> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      lines.emplace_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < text.size()) lines.emplace_back(text.substr(start));

  std::ostringstream out;
  std::int32_t depth = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::string_view raw = lines[i];
    std::string_view body = opts.trim_trailing_whitespace ? Trim(raw) : raw;

    // Lines starting with `}` reduce indent before being emitted.
    std::int32_t line_depth = depth;
    if (!body.empty() && body.front() == '}') {
      line_depth = std::max<std::int32_t>(0, depth - 1);
    }

    if (body.empty()) {
      out << '\n';
    } else {
      out << MakeIndent(static_cast<std::uint32_t>(line_depth), opts) << body
          << '\n';
    }

    // Net brace delta for the next line.
    std::int32_t delta = 0;
    bool in_string = false;
    char qc = '\0';
    for (std::size_t k = 0; k < body.size(); ++k) {
      char c = body[k];
      if (in_string) {
        if (c == '\\' && k + 1 < body.size()) { ++k; continue; }
        if (c == qc) in_string = false;
        continue;
      }
      if (c == '/' && k + 1 < body.size() && body[k + 1] == '/') break;
      if (c == '"' || c == '\'') { in_string = true; qc = c; continue; }
      if (c == '{') ++delta;
      else if (c == '}') --delta;
    }
    depth = std::max<std::int32_t>(0, depth + delta);
  }

  std::string s = out.str();
  if (opts.insert_final_newline) {
    if (s.empty() || s.back() != '\n') s.push_back('\n');
  } else {
    while (!s.empty() && s.back() == '\n') s.pop_back();
  }
  return s;
}

}  // namespace polyglot::tools::ui
