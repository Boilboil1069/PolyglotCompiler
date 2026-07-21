/**
 * @file     diff_engine.h
 * @brief    Myers-style line diff + hunk acceptance helpers
 *           (demand 2026-04-28-27 §1).
 *
 * The engine is independent of any SCM backend so the IDE can use
 * it for "compare with file" / "compare with clipboard" features in
 * addition to git diffs.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tools/ui/common/scm/scm_provider.h"

namespace polyglot::tools::ui::scm {

enum class DiffOpKind { kEqual, kInsert, kDelete };

struct DiffOp {
  DiffOpKind kind{DiffOpKind::kEqual};
  std::uint32_t old_index{0};  ///< 0-based index in the old buffer
  std::uint32_t new_index{0};  ///< 0-based index in the new buffer
  std::string text;            ///< the line content (no newline)
};

/// Computes a line-level diff between `old_text` and `new_text`
/// using a longest-common-subsequence dynamic-programming approach.
/// Suitable for typical IDE buffers (≤ a few thousand lines).
std::vector<DiffOp> DiffLines(std::string_view old_text,
                              std::string_view new_text);

/// Groups raw `DiffLines` output into unified-diff style hunks with
/// `context` lines of equal context around each change.
std::vector<DiffHunk> BuildHunks(const std::vector<DiffOp> &ops,
                                 std::uint32_t context);

/// Applies the inserted/deleted lines of `hunk` to `target_text`,
/// producing the post-acceptance buffer.  Used by the "Accept hunk"
/// UI action.  Returns the original text on failure.
std::string ApplyHunk(std::string_view target_text, const DiffHunk &hunk);

/// Reverses an apply.  Used for "Reject hunk" / unstage.
std::string RevertHunk(std::string_view target_text, const DiffHunk &hunk);

}  // namespace polyglot::tools::ui::scm
