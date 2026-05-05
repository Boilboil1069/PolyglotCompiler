/**
 * @file     snippet_engine.cpp
 * @brief    Implementation of the snippet expansion engine.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/editing/snippet_engine.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui {

namespace {

bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsVarChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
         IsDigit(c);
}

std::string LookupVar(const std::map<std::string, std::string> &vars,
                      const std::string &name) {
  auto it = vars.find(name);
  return it == vars.end() ? std::string{} : it->second;
}

}  // namespace

SnippetExpansion ExpandSnippet(std::string_view body,
                               const std::map<std::string, std::string> &vars) {
  SnippetExpansion out;
  out.text.reserve(body.size());
  for (std::size_t i = 0; i < body.size();) {
    char c = body[i];
    if (c != '$') {
      out.text.push_back(c);
      ++i;
      continue;
    }
    // `$$` literal.
    if (i + 1 < body.size() && body[i + 1] == '$') {
      out.text.push_back('$');
      i += 2;
      continue;
    }
    // `$NAME` (variable) or `$N` (tabstop).
    if (i + 1 < body.size() && IsDigit(body[i + 1])) {
      std::size_t j = i + 1;
      while (j < body.size() && IsDigit(body[j])) ++j;
      std::uint32_t idx = static_cast<std::uint32_t>(
          std::stoul(std::string(body.substr(i + 1, j - (i + 1)))));
      SnippetTabstop ts;
      ts.index = idx;
      ts.offset = static_cast<std::uint32_t>(out.text.size());
      ts.length = 0;
      out.tabstops.push_back(ts);
      i = j;
      continue;
    }
    if (i + 1 < body.size() && (std::isalpha(static_cast<unsigned char>(body[i + 1])) ||
                                body[i + 1] == '_')) {
      std::size_t j = i + 1;
      while (j < body.size() && IsVarChar(body[j])) ++j;
      std::string name(body.substr(i + 1, j - (i + 1)));
      out.text += LookupVar(vars, name);
      i = j;
      continue;
    }
    // `${...}` form.
    if (i + 1 < body.size() && body[i + 1] == '{') {
      std::size_t j = i + 2;
      // Find matching `}` (no nested `{}` allowed in this minimal
      // implementation).
      std::size_t close = body.find('}', j);
      if (close == std::string_view::npos) {
        out.text.push_back(c);
        ++i;
        continue;
      }
      std::string_view inner = body.substr(j, close - j);
      // Tabstop with default / choices.
      if (!inner.empty() && IsDigit(inner.front())) {
        std::size_t k = 0;
        while (k < inner.size() && IsDigit(inner[k])) ++k;
        std::uint32_t idx = static_cast<std::uint32_t>(
            std::stoul(std::string(inner.substr(0, k))));
        SnippetTabstop ts;
        ts.index = idx;
        ts.offset = static_cast<std::uint32_t>(out.text.size());
        std::string default_text;
        if (k < inner.size() && inner[k] == ':') {
          default_text.assign(inner.substr(k + 1));
          // Expand variables inside the default text.
          SnippetExpansion sub =
              ExpandSnippet(default_text, vars);
          default_text = sub.text;
        } else if (k < inner.size() && inner[k] == '|') {
          // Choice: `${N|a,b,c|}`.
          std::size_t end_bar = inner.rfind('|');
          if (end_bar > k) {
            std::string choices_str(inner.substr(k + 1, end_bar - k - 1));
            std::stringstream ss(choices_str);
            std::string item;
            while (std::getline(ss, item, ',')) ts.choices.push_back(item);
            if (!ts.choices.empty()) default_text = ts.choices.front();
          }
        }
        out.text += default_text;
        ts.length = static_cast<std::uint32_t>(default_text.size());
        out.tabstops.push_back(ts);
      } else {
        // `${VAR}` or `${VAR:default}`.
        std::size_t colon = inner.find(':');
        std::string name(colon == std::string_view::npos
                             ? inner
                             : inner.substr(0, colon));
        std::string val = LookupVar(vars, name);
        if (val.empty() && colon != std::string_view::npos) {
          val.assign(inner.substr(colon + 1));
        }
        out.text += val;
      }
      i = close + 1;
      continue;
    }
    // Fallback — treat the `$` literally.
    out.text.push_back(c);
    ++i;
  }

  std::stable_sort(out.tabstops.begin(), out.tabstops.end(),
                   [](const SnippetTabstop &a, const SnippetTabstop &b) {
                     return a.offset < b.offset;
                   });
  return out;
}

void SnippetLibrary::Add(SnippetEntry entry) {
  for (auto &e : entries_) {
    if (e.name == entry.name) {
      e = std::move(entry);
      return;
    }
  }
  entries_.push_back(std::move(entry));
}

std::size_t SnippetLibrary::LoadJson(std::string_view json_text) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json_text);
  } catch (const std::exception &) {
    return 0;
  }
  if (!doc.is_object()) return 0;
  std::size_t added = 0;
  for (auto it = doc.begin(); it != doc.end(); ++it) {
    if (!it.value().is_object()) continue;
    SnippetEntry e;
    e.name = it.key();
    e.prefix = it.value().value("prefix", std::string{});
    e.description = it.value().value("description", std::string{});
    auto &body = it.value()["body"];
    if (body.is_string()) {
      e.body = body.get<std::string>();
    } else if (body.is_array()) {
      std::ostringstream os;
      bool first = true;
      for (const auto &line : body) {
        if (!first) os << '\n';
        first = false;
        if (line.is_string()) os << line.get<std::string>();
      }
      e.body = os.str();
    } else {
      continue;
    }
    Add(std::move(e));
    ++added;
  }
  return added;
}

std::vector<SnippetEntry> SnippetLibrary::Match(std::string_view needle) const {
  std::vector<SnippetEntry> out;
  for (const auto &e : entries_) {
    if (e.prefix.size() >= needle.size() &&
        e.prefix.compare(0, needle.size(), needle.data(), needle.size()) == 0) {
      out.push_back(e);
    }
  }
  return out;
}

}  // namespace polyglot::tools::ui
