/**
 * @file     merge_resolver.h
 * @brief    Three-way merge conflict parser + resolver
 *           (demand 2026-04-28-27 §3).
 *
 * Parses standard `<<<<<<<` / `=======` / `>>>>>>>` conflict
 * markers (with optional `|||||||` base section) into structured
 * `MergeConflict` records and lets the IDE accept current /
 * incoming / both / custom resolutions and emit the cleaned buffer.
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

namespace polyglot::tools::ui::scm {

struct MergeConflict {
  std::uint32_t start_line{0};         ///< first marker line (0-based)
  std::uint32_t end_line{0};           ///< closing marker line
  std::string current_label;           ///< text after `<<<<<<< `
  std::string incoming_label;          ///< text after `>>>>>>> `
  std::vector<std::string> current;    ///< lines in HEAD section
  std::vector<std::string> base;       ///< lines in `|||||||` section (may be empty)
  std::vector<std::string> incoming;   ///< lines in incoming section
};

enum class MergeChoice {
  kCurrent,
  kIncoming,
  kBoth,        ///< current then incoming
  kBase,        ///< three-way base
};

/// Scans `text` for conflict markers and returns the structured
/// records.  Lines outside conflicts are not returned — use
/// `ResolveConflict` to splice the file back together.
std::vector<MergeConflict> ParseMergeConflicts(std::string_view text);

/// Returns `true` iff `text` contains any conflict marker.
bool HasMergeConflicts(std::string_view text);

/// Replaces every conflict in `text` with the chosen side (or a
/// caller-supplied custom replacement when `custom_resolutions` is
/// non-empty).  `custom_resolutions[i]` corresponds to the i-th
/// conflict in document order; an empty entry falls back to
/// `default_choice`.
std::string ResolveConflicts(std::string_view text,
                             MergeChoice default_choice,
                             const std::vector<std::vector<std::string>>
                                 &custom_resolutions = {});

}  // namespace polyglot::tools::ui::scm
