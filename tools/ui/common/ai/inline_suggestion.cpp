/**
 * @file     inline_suggestion.cpp
 * @brief    Implementation of `InlineSuggestionSession`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/ai/inline_suggestion.h"

namespace polyglot::tools::ui::ai {

std::string InlineSuggestionStateName(InlineSuggestionState s) {
  switch (s) {
    case InlineSuggestionState::kIdle:      return "idle";
    case InlineSuggestionState::kShowing:   return "showing";
    case InlineSuggestionState::kAccepted:  return "accepted";
    case InlineSuggestionState::kDismissed: return "dismissed";
  }
  return "idle";
}

void InlineSuggestionSession::Show(std::vector<std::string> alternatives) {
  alternatives_ = std::move(alternatives);
  cursor_ = 0;
  state_ = alternatives_.empty() ? InlineSuggestionState::kIdle
                                 : InlineSuggestionState::kShowing;
}

void InlineSuggestionSession::Dismiss() {
  state_ = InlineSuggestionState::kDismissed;
}

std::string InlineSuggestionSession::Accept() {
  if (state_ != InlineSuggestionState::kShowing || alternatives_.empty()) {
    return {};
  }
  std::string out = alternatives_[cursor_];
  state_ = InlineSuggestionState::kAccepted;
  return out;
}

void InlineSuggestionSession::Next() {
  if (alternatives_.empty()) return;
  cursor_ = (cursor_ + 1) % alternatives_.size();
}

void InlineSuggestionSession::Prev() {
  if (alternatives_.empty()) return;
  cursor_ = (cursor_ + alternatives_.size() - 1) % alternatives_.size();
}

std::string InlineSuggestionSession::current() const {
  if (alternatives_.empty()) return {};
  return alternatives_[cursor_];
}

}  // namespace polyglot::tools::ui::ai
