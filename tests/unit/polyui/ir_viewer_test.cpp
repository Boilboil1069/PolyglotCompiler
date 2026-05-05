/**
 * @file     ir_viewer_test.cpp
 * @brief    Unit tests for `IrModule`, `DiffFunctions`, bindings.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/pipeline/ir_viewer.h"

using namespace polyglot::tools::ui::pipeline;

TEST_CASE("IrModule::Parse splits functions and basic blocks",
          "[polyui][pipeline][ir]") {
  std::string ir =
      "define i32 @add(i32 %a, i32 %b) {\n"
      "entry:\n"
      "  %r = add i32 %a, %b\n"
      "  ret i32 %r\n"
      "}\n"
      "define i32 @sub(i32 %a, i32 %b) {\n"
      "entry:\n"
      "  %r = sub i32 %a, %b\n"
      "  ret i32 %r\n"
      "}\n";
  auto m = IrModule::Parse(ir);
  REQUIRE(m.functions().size() == 2);
  CHECK(m.functions()[0].name == "add");
  CHECK(m.functions()[1].name == "sub");
  REQUIRE(m.functions()[0].blocks.size() == 1);
  CHECK(m.functions()[0].blocks[0].label == "entry");
  CHECK(m.functions()[0].blocks[0].lines.size() == 2);
  CHECK(m.FindFunction("sub") != nullptr);
}

TEST_CASE("DiffFunctions produces equal/added/removed lines",
          "[polyui][pipeline][ir]") {
  IrFunction left;
  left.blocks.push_back({"entry", 0, 0,
                         {"  %t = add i32 %a, %b", "  ret i32 %t"}});
  IrFunction right;
  right.blocks.push_back({"entry", 0, 0,
                          {"  %t = add nsw i32 %a, %b", "  ret i32 %t"}});
  auto d = DiffFunctions(left, right);
  bool saw_removed = false, saw_added = false, saw_equal = false;
  for (const auto &l : d) {
    if (l.kind == DiffKind::kRemoved) saw_removed = true;
    if (l.kind == DiffKind::kAdded)   saw_added = true;
    if (l.kind == DiffKind::kEqual)   saw_equal = true;
  }
  CHECK(saw_removed);
  CHECK(saw_added);
  CHECK(saw_equal);
}

TEST_CASE("LineBindingTable resolves source/IR/asset round-trips",
          "[polyui][pipeline][ir]") {
  LineBindingTable t;
  LineBinding b;
  b.source_file = "main.ploy";
  b.source_line = 12;
  b.ir_line = 34;
  b.asset_file = "main.s";
  b.asset_line = 56;
  t.Add(b);

  auto from_src = t.FromSource("main.ploy", 12);
  REQUIRE(from_src);
  CHECK(from_src->ir_line == 34);
  auto from_ir = t.FromIr(34);
  REQUIRE(from_ir);
  CHECK(from_ir->asset_line == 56);
  auto from_asset = t.FromAsset("main.s", 56);
  REQUIRE(from_asset);
  CHECK(from_asset->source_line == 12);
  CHECK_FALSE(t.FromSource("missing.ploy", 1));
}
