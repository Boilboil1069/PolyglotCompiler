/**
 * @file     notebook.h
 * @brief    Notebook value model with code / markdown / link cells.
 *
 * The Notebook is the persistent backing store for the polyui
 * Notebook view.  Each cell carries its own kernel binding (one of
 * the `ReplEngine` flavours) and its last execution output.  A
 * special `kCrossLanguageLink` cell type fans the same expression
 * across two engines and records both transcripts so the IDE can
 * show the cross-language `LINK` plumbing inline.
 *
 * Serialisation uses a `.polynb` JSON envelope so notebooks are
 * diff-friendly and round-trip through Git.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "tools/ui/common/notebook/repl_session.h"

namespace polyglot::tools::ui::notebook {

enum class CellKind {
  kCode,
  kMarkdown,
  kCrossLanguageLink,
};

struct CellOutput {
  std::string stdout_text;
  std::string stderr_text;
  bool error{false};
};

struct LinkBinding {
  ReplEngine target_engine{ReplEngine::kPloy};
  ReplEngine source_engine{ReplEngine::kPython};
  std::string target_symbol;
  std::string source_symbol;
};

struct Cell {
  std::string id;
  CellKind kind{CellKind::kCode};
  ReplEngine engine{ReplEngine::kPloy};
  std::string source;
  CellOutput output;
  LinkBinding link;          ///< Only meaningful for kCrossLanguageLink.
};

class Notebook {
 public:
  /// Append a new cell.  Returns the cell id (auto-generated when
  /// the caller leaves it blank).
  std::string AddCell(Cell cell);
  bool RemoveCell(const std::string &id);
  bool MoveCell(const std::string &id, int new_index);
  Cell *Find(const std::string &id);
  const Cell *Find(const std::string &id) const;

  const std::vector<Cell> &cells() const { return cells_; }

  /// Execute one cell using the supplied session map.  Sessions
  /// must already be `Start()`ed; the notebook does not own them.
  /// Returns the freshly captured `CellOutput`.
  CellOutput Execute(const std::string &id,
                     std::unordered_map<ReplEngine, ReplSession *> &sessions);

  /// Execute every code / link cell sequentially.  Markdown cells
  /// are skipped.
  void ExecuteAll(std::unordered_map<ReplEngine, ReplSession *> &sessions);

  /// Serialise to a `.polynb` JSON document.
  std::string ToJson() const;

  /// Inverse of `ToJson`.  Returns false on parse error.
  bool LoadJson(const std::string &text);

 private:
  std::vector<Cell> cells_;
  std::size_t next_id_{1};
};

std::string ReplEngineName(ReplEngine e);
ReplEngine ReplEngineFromName(const std::string &name);

}  // namespace polyglot::tools::ui::notebook
