/**
 * @file     inlay_hints_test.cpp
 * @brief    Unit tests for `InlayHintProvider`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/inlay_hints/inlay_hints.h"

using namespace polyglot::tools::ui::inlay;

TEST_CASE("InlayKind round-trips", "[polyui][inlay]") {
  CHECK(*InlayKindFromName(InlayKindName(InlayKind::kType)) == InlayKind::kType);
  CHECK(*InlayKindFromName(InlayKindName(InlayKind::kParameter)) ==
        InlayKind::kParameter);
  CHECK_FALSE(InlayKindFromName("nope"));
}

TEST_CASE("Produce emits both type and parameter hints by default",
          "[polyui][inlay]") {
  InlayHintProvider p;
  Declaration m;
  m.name = "m";
  m.inferred_type = "HANDLE<python::torch::nn::Linear>";
  m.name_end = {3, 5};
  CallArgument x{"x", {7, 4}};
  CallArgument y{"y", {7, 7}};

  auto hints = p.Produce({m}, {x, y});
  REQUIRE(hints.size() == 3);

  CHECK(hints[0].kind == InlayKind::kType);
  CHECK(hints[0].label == ": HANDLE<python::torch::nn::Linear>");
  CHECK(hints[0].position.line == 3);
  CHECK(hints[0].padding_left);

  CHECK(hints[1].kind == InlayKind::kParameter);
  CHECK(hints[1].label == "x:");
  CHECK(hints[1].padding_right);
  CHECK(hints[2].label == "y:");
}

TEST_CASE("Settings can disable each hint family independently",
          "[polyui][inlay]") {
  InlayHintProvider p;
  Declaration d{"a", "INT", {0, 1}};
  CallArgument c{"k", {0, 5}};

  p.set_settings({false, true});
  auto h1 = p.Produce({d}, {c});
  REQUIRE(h1.size() == 1);
  CHECK(h1[0].kind == InlayKind::kParameter);

  p.set_settings({true, false});
  auto h2 = p.Produce({d}, {c});
  REQUIRE(h2.size() == 1);
  CHECK(h2[0].kind == InlayKind::kType);

  p.set_settings({false, false});
  CHECK(p.Produce({d}, {c}).empty());
}

TEST_CASE("Empty inferred type / parameter name are skipped",
          "[polyui][inlay]") {
  InlayHintProvider p;
  Declaration d{"a", "", {0, 1}};
  CallArgument c{"", {0, 5}};
  CHECK(p.Produce({d}, {c}).empty());
}
