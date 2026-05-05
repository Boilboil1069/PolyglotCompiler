/**
 * @file     outline_model.cpp
 * @brief    Implementation of `OutlineModel` (demand 2026-04-28-25 §5).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/outline/outline_model.h"

#include <algorithm>
#include <cctype>

namespace polyglot::tools::ui {

namespace {

std::string ToLower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

bool ContainsCi(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  const std::string lhay = ToLower(hay);
  const std::string lneedle = ToLower(needle);
  return lhay.find(lneedle) != std::string::npos;
}

bool SubtreeMatches(const OutlineNode &n, std::string_view needle) {
  if (ContainsCi(n.name, needle)) return true;
  for (const auto &c : n.children) {
    if (SubtreeMatches(*c, needle)) return true;
  }
  return false;
}

}  // namespace

void OutlineModel::SetRoots(
    std::vector<std::unique_ptr<OutlineNode>> roots) {
  roots_ = std::move(roots);
}

void OutlineModel::SetFilter(std::string needle) {
  filter_ = std::move(needle);
}

bool OutlineModel::Matches(const OutlineNode &n) const {
  return SubtreeMatches(n, filter_);
}

std::vector<const OutlineNode *> OutlineModel::Visible() const {
  std::vector<const OutlineNode *> out;
  out.reserve(roots_.size());
  for (const auto &r : roots_) {
    if (Matches(*r)) out.push_back(r.get());
  }
  return out;
}

namespace {

const OutlineNode *FindEnclosing(const OutlineNode &n, std::uint32_t line) {
  if (n.line > line) return nullptr;
  // Pick the deepest child whose start line is also <= line.  This is
  // a coarse approximation that matches how IDEs render breadcrumbs
  // when ranges are unavailable from the LSP payload.
  const OutlineNode *deepest = nullptr;
  for (const auto &c : n.children) {
    if (c->line <= line) {
      const OutlineNode *inner = FindEnclosing(*c, line);
      deepest = inner ? inner : c.get();
    }
  }
  return deepest ? deepest : &n;
}

}  // namespace

std::vector<const OutlineNode *> OutlineModel::Breadcrumbs(
    std::uint32_t line, std::uint32_t /*character*/) const {
  std::vector<const OutlineNode *> trail;
  for (const auto &r : roots_) {
    if (r->line > line) continue;
    const OutlineNode *cursor = r.get();
    trail.push_back(cursor);
    while (true) {
      const OutlineNode *next = nullptr;
      for (const auto &c : cursor->children) {
        if (c->line <= line) next = c.get();
      }
      if (!next) break;
      trail.push_back(next);
      cursor = next;
    }
  }
  return trail;
}

}  // namespace polyglot::tools::ui
