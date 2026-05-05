/**
 * @file     format_engine.h
 * @brief    Brace-aware Ploy formatter and shared formatter options
 *           (demand 2026-04-28-26 §3).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace polyglot::tools::ui {

struct FormatOptions {
  std::uint32_t tab_size{4};
  bool insert_spaces{true};
  bool trim_trailing_whitespace{true};
  bool insert_final_newline{true};
};

/// Re-indents `text` according to brace nesting.  The formatter:
///   * normalises leading whitespace to `tab_size` units of indent
///     per `{` open level (or hard tabs when `insert_spaces` is
///     `false`);
///   * strips trailing whitespace per line when requested;
///   * guarantees a single trailing newline when requested.
/// The formatter does not reflow tokens — it preserves the original
/// (trimmed) line content verbatim except for its leading indent.
std::string FormatPloy(std::string_view text, const FormatOptions &opts);

}  // namespace polyglot::tools::ui
