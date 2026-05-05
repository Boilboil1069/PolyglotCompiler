/**
 * @file     notebook.cpp
 * @brief    Implementation of `notebook.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/notebook/notebook.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::notebook {
namespace {

using Json = nlohmann::json;

CellKind KindFromName(const std::string &n) {
  if (n == "markdown") return CellKind::kMarkdown;
  if (n == "link")     return CellKind::kCrossLanguageLink;
  return CellKind::kCode;
}

std::string KindName(CellKind k) {
  switch (k) {
    case CellKind::kMarkdown:           return "markdown";
    case CellKind::kCrossLanguageLink:  return "link";
    case CellKind::kCode:               break;
  }
  return "code";
}

}  // namespace

std::string ReplEngineName(ReplEngine e) {
  switch (e) {
    case ReplEngine::kPloy:         return "ploy";
    case ReplEngine::kPython:       return "python";
    case ReplEngine::kIRust:        return "irust";
    case ReplEngine::kIRB:          return "irb";
    case ReplEngine::kDotnetScript: return "dotnet-script";
  }
  return "unknown";
}

ReplEngine ReplEngineFromName(const std::string &name) {
  if (name == "python")        return ReplEngine::kPython;
  if (name == "irust")         return ReplEngine::kIRust;
  if (name == "irb")           return ReplEngine::kIRB;
  if (name == "dotnet-script") return ReplEngine::kDotnetScript;
  return ReplEngine::kPloy;
}

std::string Notebook::AddCell(Cell cell) {
  if (cell.id.empty())
    cell.id = "cell-" + std::to_string(next_id_++);
  cells_.push_back(std::move(cell));
  return cells_.back().id;
}

bool Notebook::RemoveCell(const std::string &id) {
  auto it = std::find_if(cells_.begin(), cells_.end(),
                         [&](const Cell &c) { return c.id == id; });
  if (it == cells_.end()) return false;
  cells_.erase(it);
  return true;
}

bool Notebook::MoveCell(const std::string &id, int new_index) {
  if (new_index < 0) return false;
  auto it = std::find_if(cells_.begin(), cells_.end(),
                         [&](const Cell &c) { return c.id == id; });
  if (it == cells_.end()) return false;
  Cell tmp = std::move(*it);
  cells_.erase(it);
  if (new_index >= static_cast<int>(cells_.size()))
    cells_.push_back(std::move(tmp));
  else
    cells_.insert(cells_.begin() + new_index, std::move(tmp));
  return true;
}

Cell *Notebook::Find(const std::string &id) {
  for (auto &c : cells_) if (c.id == id) return &c;
  return nullptr;
}

const Cell *Notebook::Find(const std::string &id) const {
  for (const auto &c : cells_) if (c.id == id) return &c;
  return nullptr;
}

CellOutput Notebook::Execute(
    const std::string &id,
    std::unordered_map<ReplEngine, ReplSession *> &sessions) {
  auto *cell = Find(id);
  if (!cell || cell->kind == CellKind::kMarkdown) return {};
  CellOutput o;

  if (cell->kind == CellKind::kCrossLanguageLink) {
    auto t_it = sessions.find(cell->link.target_engine);
    auto s_it = sessions.find(cell->link.source_engine);
    if (s_it == sessions.end() || t_it == sessions.end()) {
      o.error = true;
      o.stderr_text = "missing session for cross-language link";
      cell->output = o;
      return o;
    }
    // Source side first (produces a value), then target side
    // consumes it through whatever bridge the engines have set up.
    const auto &src = s_it->second->Eval(cell->link.source_symbol);
    const auto &tgt = t_it->second->Eval(cell->link.target_symbol);
    o.stdout_text = src.stdout_text + "\n" + tgt.stdout_text;
    o.stderr_text = src.stderr_text + tgt.stderr_text;
    o.error = src.error || tgt.error;
    cell->output = o;
    return o;
  }

  auto it = sessions.find(cell->engine);
  if (it == sessions.end()) {
    o.error = true;
    o.stderr_text = "no session for engine";
    cell->output = o;
    return o;
  }
  const auto &turn = it->second->Eval(cell->source);
  o.stdout_text = turn.stdout_text;
  o.stderr_text = turn.stderr_text;
  o.error = turn.error;
  cell->output = o;
  return o;
}

void Notebook::ExecuteAll(
    std::unordered_map<ReplEngine, ReplSession *> &sessions) {
  for (auto &c : cells_) {
    if (c.kind == CellKind::kMarkdown) continue;
    Execute(c.id, sessions);
  }
}

std::string Notebook::ToJson() const {
  Json doc;
  doc["format"] = "polynb";
  doc["version"] = 1;
  Json arr = Json::array();
  for (const auto &c : cells_) {
    Json j;
    j["id"] = c.id;
    j["kind"] = KindName(c.kind);
    j["engine"] = ReplEngineName(c.engine);
    j["source"] = c.source;
    j["output"] = {{"stdout", c.output.stdout_text},
                   {"stderr", c.output.stderr_text},
                   {"error", c.output.error}};
    if (c.kind == CellKind::kCrossLanguageLink) {
      j["link"] = {
          {"target_engine", ReplEngineName(c.link.target_engine)},
          {"source_engine", ReplEngineName(c.link.source_engine)},
          {"target_symbol", c.link.target_symbol},
          {"source_symbol", c.link.source_symbol},
      };
    }
    arr.push_back(std::move(j));
  }
  doc["cells"] = std::move(arr);
  return doc.dump(2);
}

bool Notebook::LoadJson(const std::string &text) {
  auto j = Json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  cells_.clear();
  next_id_ = 1;
  if (!j.contains("cells") || !j["cells"].is_array()) return true;
  for (const auto &c : j["cells"]) {
    Cell cell;
    cell.id = c.value("id", std::string{});
    cell.kind = KindFromName(c.value("kind", std::string{"code"}));
    cell.engine = ReplEngineFromName(c.value("engine", std::string{"ploy"}));
    cell.source = c.value("source", std::string{});
    if (c.contains("output") && c["output"].is_object()) {
      cell.output.stdout_text = c["output"].value("stdout", std::string{});
      cell.output.stderr_text = c["output"].value("stderr", std::string{});
      cell.output.error = c["output"].value("error", false);
    }
    if (cell.kind == CellKind::kCrossLanguageLink && c.contains("link")) {
      cell.link.target_engine = ReplEngineFromName(
          c["link"].value("target_engine", std::string{"ploy"}));
      cell.link.source_engine = ReplEngineFromName(
          c["link"].value("source_engine", std::string{"python"}));
      cell.link.target_symbol = c["link"].value("target_symbol", std::string{});
      cell.link.source_symbol = c["link"].value("source_symbol", std::string{});
    }
    cells_.push_back(std::move(cell));
  }
  return true;
}

}  // namespace polyglot::tools::ui::notebook
