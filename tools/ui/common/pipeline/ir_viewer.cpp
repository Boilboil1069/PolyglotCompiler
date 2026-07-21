/**
 * @file     ir_viewer.cpp
 * @brief    Implementation of `ir_viewer.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/pipeline/ir_viewer.h"

#include <algorithm>
#include <sstream>

namespace polyglot::tools::ui::pipeline {
namespace {

bool IsFunctionHeader(const std::string &line, std::string &name) {
  // Accept "define <ret> @name(...)" (LLVM-style) and
  // "func @name(...)" (MLIR-style).
  std::string trimmed;
  for (char c : line) {
    if (c == '\r') continue;
    trimmed.push_back(c);
  }
  auto starts_with = [&](const std::string &p) {
    return trimmed.size() >= p.size() && trimmed.compare(0, p.size(), p) == 0;
  };
  std::size_t at = std::string::npos;
  if (starts_with("define "))   at = trimmed.find('@');
  else if (starts_with("func ")) at = trimmed.find('@');
  if (at == std::string::npos)  return false;
  std::size_t end = at + 1;
  while (end < trimmed.size() &&
         (std::isalnum(static_cast<unsigned char>(trimmed[end])) ||
          trimmed[end] == '_' || trimmed[end] == '.' || trimmed[end] == ':'))
    ++end;
  name = trimmed.substr(at + 1, end - at - 1);
  return !name.empty();
}

bool IsBlockLabel(const std::string &line, std::string &label) {
  // Accept "bb0:" / "entry:" — non-empty identifier followed by colon
  // at the start of the line, no leading whitespace.
  if (line.empty() || std::isspace(static_cast<unsigned char>(line.front())))
    return false;
  auto colon = line.find(':');
  if (colon == std::string::npos) return false;
  for (std::size_t i = 0; i < colon; ++i) {
    char c = line[i];
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'))
      return false;
  }
  label = line.substr(0, colon);
  return true;
}

}  // namespace

IrModule IrModule::Parse(const std::string &text) {
  IrModule m;
  std::stringstream ss(text);
  std::string line;
  int line_no = 0;
  IrFunction *current_fn = nullptr;
  IrBasicBlock *current_bb = nullptr;
  while (std::getline(ss, line)) {
    ++line_no;
    std::string name;
    if (IsFunctionHeader(line, name)) {
      IrFunction fn;
      fn.name = name;
      fn.start_line = line_no;
      fn.end_line = line_no;
      m.functions_.push_back(std::move(fn));
      current_fn = &m.functions_.back();
      current_bb = nullptr;
      continue;
    }
    if (!current_fn) continue;
    current_fn->end_line = line_no;
    std::string label;
    if (IsBlockLabel(line, label)) {
      IrBasicBlock bb;
      bb.label = label;
      bb.start_line = line_no;
      bb.end_line = line_no;
      current_fn->blocks.push_back(std::move(bb));
      current_bb = &current_fn->blocks.back();
      continue;
    }
    if (line == "}") {
      current_bb = nullptr;
      continue;
    }
    if (current_bb) {
      current_bb->end_line = line_no;
      current_bb->lines.push_back(line);
    }
  }
  return m;
}

const IrFunction *IrModule::FindFunction(const std::string &name) const {
  for (const auto &f : functions_)
    if (f.name == name) return &f;
  return nullptr;
}

std::vector<DiffLine> DiffFunctions(const IrFunction &left,
                                    const IrFunction &right) {
  std::vector<std::string> a, b;
  for (const auto &bb : left.blocks)
    for (const auto &l : bb.lines) a.push_back(l);
  for (const auto &bb : right.blocks)
    for (const auto &l : bb.lines) b.push_back(l);

  // Classic LCS table over line strings; output forward.
  const std::size_t n = a.size(), m = b.size();
  std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
  for (std::size_t i = 0; i < n; ++i)
    for (std::size_t j = 0; j < m; ++j)
      dp[i + 1][j + 1] = (a[i] == b[j])
          ? dp[i][j] + 1
          : std::max(dp[i + 1][j], dp[i][j + 1]);

  std::vector<DiffLine> rev;
  std::size_t i = n, j = m;
  while (i > 0 && j > 0) {
    if (a[i - 1] == b[j - 1]) {
      DiffLine d;
      d.kind = DiffKind::kEqual;
      d.text = a[i - 1];
      d.left_line = static_cast<int>(i);
      d.right_line = static_cast<int>(j);
      rev.push_back(std::move(d));
      --i; --j;
    } else if (dp[i][j - 1] >= dp[i - 1][j]) {
      DiffLine d;
      d.kind = DiffKind::kAdded;
      d.text = b[j - 1];
      d.right_line = static_cast<int>(j);
      rev.push_back(std::move(d));
      --j;
    } else {
      DiffLine d;
      d.kind = DiffKind::kRemoved;
      d.text = a[i - 1];
      d.left_line = static_cast<int>(i);
      rev.push_back(std::move(d));
      --i;
    }
  }
  while (i > 0) {
    DiffLine d;
    d.kind = DiffKind::kRemoved;
    d.text = a[i - 1];
    d.left_line = static_cast<int>(i);
    rev.push_back(std::move(d));
    --i;
  }
  while (j > 0) {
    DiffLine d;
    d.kind = DiffKind::kAdded;
    d.text = b[j - 1];
    d.right_line = static_cast<int>(j);
    rev.push_back(std::move(d));
    --j;
  }
  std::reverse(rev.begin(), rev.end());
  return rev;
}

void LineBindingTable::Add(LineBinding b) {
  entries_.push_back(std::move(b));
}

std::optional<LineBinding> LineBindingTable::FromSource(
    const std::string &file, int line) const {
  for (const auto &b : entries_)
    if (b.source_file == file && b.source_line == line) return b;
  return std::nullopt;
}

std::optional<LineBinding> LineBindingTable::FromIr(int ir_line) const {
  for (const auto &b : entries_)
    if (b.ir_line == ir_line) return b;
  return std::nullopt;
}

std::optional<LineBinding> LineBindingTable::FromAsset(
    const std::string &file, int line) const {
  for (const auto &b : entries_)
    if (b.asset_file == file && b.asset_line == line) return b;
  return std::nullopt;
}

}  // namespace polyglot::tools::ui::pipeline
