/**
 * @file     inline_suggestion.h
 * @brief    State machine for inline (ghost-text) AI suggestions.
 *
 * Owns the alternatives returned by the provider and tracks the
 * current pick.  The IDE binds Tab → `Accept`, Esc → `Dismiss`,
 * Alt+] → `Next`, Alt+[ → `Prev`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <string>
#include <vector>

namespace polyglot::tools::ui::ai {

enum class InlineSuggestionState {
  kIdle,
  kShowing,
  kAccepted,
  kDismissed,
};

std::string InlineSuggestionStateName(InlineSuggestionState s);

class InlineSuggestionSession {
 public:
  void Show(std::vector<std::string> alternatives);
  void Dismiss();
  std::string Accept();   ///< Returns the inserted text or empty.
  void Next();
  void Prev();

  InlineSuggestionState state() const { return state_; }
  size_t alternative_count() const { return alternatives_.size(); }
  size_t cursor() const { return cursor_; }
  std::string current() const;

 private:
  std::vector<std::string> alternatives_;
  size_t cursor_{0};
  InlineSuggestionState state_{InlineSuggestionState::kIdle};
};

}  // namespace polyglot::tools::ui::ai
