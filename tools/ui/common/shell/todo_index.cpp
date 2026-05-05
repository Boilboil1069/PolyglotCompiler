/**
 * @file     todo_index.cpp
 * @brief    TODO / FIXME index implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/shell/todo_index.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace polyglot::tools::ui::shell {

namespace {

bool IsWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string Trim(const std::string &s) {
  size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

}  // namespace

TodoIndex::TodoIndex() : keywords_{"TODO", "FIXME"} {}

void TodoIndex::set_keywords(std::vector<std::string> keywords) {
  keywords_ = std::move(keywords);
}

size_t TodoIndex::Scan(const std::string &path, const std::string &text) {
  by_path_.erase(path);
  std::vector<TodoItem> hits;
  std::istringstream is(text);
  std::string line;
  long long ln = 0;
  while (std::getline(is, line)) {
    ++ln;
    for (const auto &kw : keywords_) {
      if (kw.empty()) continue;
      size_t pos = 0;
      while ((pos = line.find(kw, pos)) != std::string::npos) {
        bool left_ok = (pos == 0) || !IsWordChar(line[pos - 1]);
        size_t end = pos + kw.size();
        bool right_ok = (end >= line.size()) || !IsWordChar(line[end]);
        if (left_ok && right_ok) {
          std::string rest = line.substr(end);
          // Optional ":" after the keyword.
          if (!rest.empty() && rest[0] == ':') rest.erase(0, 1);
          TodoItem it;
          it.keyword = kw;
          it.path = path;
          it.line = ln;
          it.text = Trim(rest);
          hits.push_back(std::move(it));
          pos = end;
        } else {
          pos = end;
        }
      }
    }
  }
  size_t n = hits.size();
  if (!hits.empty()) by_path_[path] = std::move(hits);
  return n;
}

void TodoIndex::Forget(const std::string &path) { by_path_.erase(path); }
void TodoIndex::Clear() { by_path_.clear(); }

std::vector<TodoItem> TodoIndex::All() const {
  std::vector<TodoItem> out;
  for (const auto &kv : by_path_)
    for (const auto &x : kv.second) out.push_back(x);
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) {
              if (a.path != b.path) return a.path < b.path;
              return a.line < b.line;
            });
  return out;
}

std::vector<TodoItem> TodoIndex::InFile(const std::string &path) const {
  auto it = by_path_.find(path);
  if (it == by_path_.end()) return {};
  return it->second;
}

std::vector<TodoItem> TodoIndex::ForKeyword(const std::string &kw) const {
  std::vector<TodoItem> out;
  for (const auto &kv : by_path_)
    for (const auto &x : kv.second)
      if (x.keyword == kw) out.push_back(x);
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) {
              if (a.path != b.path) return a.path < b.path;
              return a.line < b.line;
            });
  return out;
}

std::unordered_map<std::string, size_t> TodoIndex::CountsByKeyword() const {
  std::unordered_map<std::string, size_t> out;
  for (const auto &kw : keywords_) out[kw] = 0;
  for (const auto &kv : by_path_)
    for (const auto &x : kv.second) ++out[x.keyword];
  return out;
}

}  // namespace polyglot::tools::ui::shell
