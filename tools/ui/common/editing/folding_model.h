/**
 * @file     folding_model.h
 * @brief    Brace + region-marker folding model
 *           (demand 2026-04-28-26 §2).
 *
 * Computes fold ranges from raw text using a brace-balanced scan
 * plus `// region` / `// endregion` markers and multi-line C-style
 * block comments.  The model is language-agnostic: any C-family
 * brace style works and Ploy uses the same `{ }` delimiters.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace polyglot::tools::ui {

enum class FoldKind {
  kBlock,    ///< `{` … `}` block.
  kComment,  ///< `/* … */` multi-line comment.
  kRegion,   ///< `// region NAME` … `// endregion`.
};

struct FoldRange {
  FoldKind kind{FoldKind::kBlock};
  std::uint32_t start_line{0};  ///< Line that opens the fold.
  std::uint32_t end_line{0};    ///< Line that closes the fold.
};

/// Computes fold ranges for `text`.  Folds with `end_line ==
/// start_line` are dropped (a fold must span at least two lines to
/// be useful in the gutter).
std::vector<FoldRange> ComputeFolds(std::string_view text);

}  // namespace polyglot::tools::ui
