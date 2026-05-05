/**
 * @file     ir_viewer.h
 * @brief    IR viewer with function/basic-block folding and diff.
 *
 * The IR Viewer ingests a textual IR dump (one function per
 * `define`/`func` block, basic blocks marked by `bbN:` labels) and
 * exposes both a folded, browsable structure and a line-level diff
 * between pre-opt and post-opt versions of the same function.
 *
 * Source ↔ IR ↔ asset line bindings are first-class so the IDE can
 * implement the three-way navigation the demand requires.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::pipeline {

struct IrBasicBlock {
  std::string label;
  int start_line{0};
  int end_line{0};
  std::vector<std::string> lines;
};

struct IrFunction {
  std::string name;
  int start_line{0};
  int end_line{0};
  std::vector<IrBasicBlock> blocks;
};

class IrModule {
 public:
  /// Parse a textual IR dump into functions + basic blocks.
  static IrModule Parse(const std::string &text);

  const std::vector<IrFunction> &functions() const { return functions_; }
  const IrFunction *FindFunction(const std::string &name) const;

 private:
  std::vector<IrFunction> functions_;
};

enum class DiffKind {
  kEqual,
  kAdded,
  kRemoved,
};

struct DiffLine {
  DiffKind kind{DiffKind::kEqual};
  std::string text;
  int left_line{0};
  int right_line{0};
};

/// Line-level diff between two IR function bodies.
std::vector<DiffLine> DiffFunctions(const IrFunction &left,
                                    const IrFunction &right);

/// Three-way binding source ↔ IR ↔ asset.
struct LineBinding {
  std::string source_file;
  int source_line{0};
  int ir_line{0};
  std::string asset_file;
  int asset_line{0};
};

class LineBindingTable {
 public:
  void Add(LineBinding b);

  std::optional<LineBinding> FromSource(const std::string &file,
                                        int line) const;
  std::optional<LineBinding> FromIr(int ir_line) const;
  std::optional<LineBinding> FromAsset(const std::string &file,
                                       int line) const;

  const std::vector<LineBinding> &all() const { return entries_; }

 private:
  std::vector<LineBinding> entries_;
};

}  // namespace polyglot::tools::ui::pipeline
