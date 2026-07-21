/**
 * @file     asm_viewer.cpp
 * @brief    Implementation of `asm_viewer.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/pipeline/asm_viewer.h"

#include <cctype>
#include <sstream>
#include <unordered_map>

namespace polyglot::tools::ui::pipeline {
namespace {

bool IsLabel(const std::string &line, std::string &name) {
  if (line.empty() || std::isspace(static_cast<unsigned char>(line.front())))
    return false;
  auto colon = line.find(':');
  if (colon == std::string::npos) return false;
  for (std::size_t i = 0; i < colon; ++i) {
    char c = line[i];
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
          c == '.' || c == '$'))
      return false;
  }
  name = line.substr(0, colon);
  return !name.empty();
}

// Parse `.loc <id> <line>` (`.loc 1 7 0`) — return (file_id, line)
// when matched.
bool ParseLocDirective(const std::string &line, int &file_id, int &src_line) {
  std::size_t i = 0;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  if (line.compare(i, 5, ".loc ") != 0 && line.compare(i, 5, ".loc\t") != 0)
    return false;
  i += 5;
  std::string a, b;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
    a.push_back(line[i++]);
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
    b.push_back(line[i++]);
  if (a.empty() || b.empty()) return false;
  file_id = std::stoi(a);
  src_line = std::stoi(b);
  return true;
}

// Parse `.file <id> "name"` — populates the file map.
bool ParseFileDirective(const std::string &line, int &file_id,
                        std::string &name) {
  std::size_t i = 0;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  if (line.compare(i, 6, ".file ") != 0 && line.compare(i, 6, ".file\t") != 0)
    return false;
  i += 6;
  std::string a;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
    a.push_back(line[i++]);
  if (a.empty()) return false;
  while (i < line.size() && line[i] != '"') ++i;
  if (i >= line.size()) return false;
  ++i;
  std::string n;
  while (i < line.size() && line[i] != '"') n.push_back(line[i++]);
  file_id = std::stoi(a);
  name = std::move(n);
  return true;
}

// Parse inline polyasm hint `; src=<file>:<line>` anywhere in the
// line; updates the (file,line) pair on success.
bool ParseInlineHint(const std::string &line, std::string &file,
                     int &src_line) {
  auto p = line.find("; src=");
  if (p == std::string::npos) p = line.find("// src=");
  if (p == std::string::npos) return false;
  auto eq = line.find('=', p);
  if (eq == std::string::npos) return false;
  auto colon = line.find(':', eq);
  if (colon == std::string::npos) return false;
  file = line.substr(eq + 1, colon - eq - 1);
  std::string n;
  std::size_t k = colon + 1;
  while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k])))
    n.push_back(line[k++]);
  if (n.empty()) return false;
  src_line = std::stoi(n);
  return true;
}

}  // namespace

std::string AsmTargetName(AsmTarget t) {
  switch (t) {
    case AsmTarget::kX86_64: return "x86_64";
    case AsmTarget::kArm64:  return "arm64";
    case AsmTarget::kWasm:   return "wasm";
  }
  return "unknown";
}

std::optional<AsmTarget> AsmTargetFromName(const std::string &name) {
  if (name == "x86_64" || name == "x64" || name == "amd64")
    return AsmTarget::kX86_64;
  if (name == "arm64" || name == "aarch64") return AsmTarget::kArm64;
  if (name == "wasm" || name == "wasm32")   return AsmTarget::kWasm;
  return std::nullopt;
}

AsmModule AsmModule::Parse(AsmTarget target, const std::string &text) {
  AsmModule m;
  m.target_ = target;
  std::unordered_map<int, std::string> files;
  std::string current_file;
  int current_line = 0;

  std::stringstream ss(text);
  std::string line;
  int line_no = 0;
  AsmFunction *current_fn = nullptr;
  while (std::getline(ss, line)) {
    ++line_no;

    int file_id = 0;
    std::string fname;
    if (ParseFileDirective(line, file_id, fname)) {
      files[file_id] = fname;
      continue;
    }
    int loc_line = 0;
    if (ParseLocDirective(line, file_id, loc_line)) {
      auto it = files.find(file_id);
      current_file = it == files.end() ? std::string{} : it->second;
      current_line = loc_line;
      continue;
    }

    std::string fn_name;
    if (IsLabel(line, fn_name)) {
      AsmFunction fn;
      fn.name = fn_name;
      fn.start_line = line_no;
      fn.end_line = line_no;
      m.functions_.push_back(std::move(fn));
      current_fn = &m.functions_.back();
      continue;
    }

    if (!current_fn) continue;
    current_fn->end_line = line_no;
    AsmLine al;
    al.line_no = line_no;
    al.text = line;
    al.source_file = current_file;
    al.source_line = current_line;
    std::string inline_file;
    int inline_line = 0;
    if (ParseInlineHint(line, inline_file, inline_line)) {
      al.source_file = inline_file;
      al.source_line = inline_line;
    }
    current_fn->lines.push_back(std::move(al));
  }
  return m;
}

const AsmFunction *AsmModule::FindFunction(const std::string &name) const {
  for (const auto &f : functions_)
    if (f.name == name) return &f;
  return nullptr;
}

std::vector<const AsmLine *> AsmModule::AsmForSource(
    const std::string &file, int line) const {
  std::vector<const AsmLine *> out;
  for (const auto &fn : functions_) {
    for (const auto &l : fn.lines) {
      if (l.source_file == file && l.source_line == line)
        out.push_back(&l);
    }
  }
  return out;
}

std::optional<std::pair<std::string, int>> AsmModule::SourceForAsm(
    const std::string &function, int line_no) const {
  const auto *fn = FindFunction(function);
  if (!fn) return std::nullopt;
  for (const auto &l : fn->lines) {
    if (l.line_no == line_no) {
      if (l.source_file.empty() && l.source_line == 0) return std::nullopt;
      return std::make_pair(l.source_file, l.source_line);
    }
  }
  return std::nullopt;
}

}  // namespace polyglot::tools::ui::pipeline
