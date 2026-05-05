/**
 * @file     inline_suggestion_test.cpp
 * @brief    Unit tests for `InlineSuggestionSession`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/ai/inline_suggestion.h"

using namespace polyglot::tools::ui::ai;

TEST_CASE("Empty suggestion list keeps the session idle",
          "[polyui][ai][inline]") {
  InlineSuggestionSession s;
  s.Show({});
  CHECK(s.state() == InlineSuggestionState::kIdle);
  CHECK(s.Accept().empty());
}

TEST_CASE("Cycle Next/Prev wraps around", "[polyui][ai][inline]") {
  InlineSuggestionSession s;
  s.Show({"a", "b", "c"});
  CHECK(s.state() == InlineSuggestionState::kShowing);
  CHECK(s.current() == "a");
  s.Next();
  CHECK(s.current() == "b");
  s.Next();
  s.Next();   // wraps to a
  CHECK(s.current() == "a");
  s.Prev();
  CHECK(s.current() == "c");
}

TEST_CASE("Accept commits the current alternative",
          "[polyui][ai][inline]") {
  InlineSuggestionSession s;
  s.Show({"alpha", "beta"});
  s.Next();
  CHECK(s.Accept() == "beta");
  CHECK(s.state() == InlineSuggestionState::kAccepted);
  // Subsequent accepts return empty.
  CHECK(s.Accept().empty());
}

TEST_CASE("Dismiss leaves no inserted text",
          "[polyui][ai][inline]") {
  InlineSuggestionSession s;
  s.Show({"alpha"});
  s.Dismiss();
  CHECK(s.state() == InlineSuggestionState::kDismissed);
  CHECK(s.Accept().empty());
}
