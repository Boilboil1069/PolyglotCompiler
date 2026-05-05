/**
 * @file     asm_viewer_test.cpp
 * @brief    Unit tests for `AsmModule` source-asm linkage.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/pipeline/asm_viewer.h"

using namespace polyglot::tools::ui::pipeline;

TEST_CASE("AsmTarget names round-trip", "[polyui][pipeline][asm]") {
  for (auto t : {AsmTarget::kX86_64, AsmTarget::kArm64, AsmTarget::kWasm})
    CHECK(*AsmTargetFromName(AsmTargetName(t)) == t);
  CHECK(*AsmTargetFromName("aarch64") == AsmTarget::kArm64);
  CHECK_FALSE(AsmTargetFromName("z80"));
}

TEST_CASE("Parse uses .file/.loc directives to bind source lines",
          "[polyui][pipeline][asm]") {
  std::string asm_text =
      ".file 1 \"main.ploy\"\n"
      "add:\n"
      ".loc 1 7\n"
      "  movl %edi, %eax\n"
      "  addl %esi, %eax\n"
      ".loc 1 8\n"
      "  retq\n";
  auto m = AsmModule::Parse(AsmTarget::kX86_64, asm_text);
  REQUIRE(m.functions().size() == 1);
  CHECK(m.functions()[0].name == "add");
  auto hits = m.AsmForSource("main.ploy", 7);
  CHECK(hits.size() == 2);
  auto src = m.SourceForAsm("add", 7);
  REQUIRE(src);
  CHECK(src->first == "main.ploy");
  CHECK(src->second == 8);
}

TEST_CASE("Parse honours inline polyasm `; src=` hints",
          "[polyui][pipeline][asm]") {
  std::string asm_text =
      "fn:\n"
      "  ld x0, [sp]   ; src=app.ploy:42\n"
      "  bl helper     ; src=app.ploy:43\n";
  auto m = AsmModule::Parse(AsmTarget::kArm64, asm_text);
  REQUIRE(m.functions().size() == 1);
  auto src = m.SourceForAsm("fn", 2);
  REQUIRE(src);
  CHECK(src->first == "app.ploy");
  CHECK(src->second == 42);
  CHECK(m.AsmForSource("app.ploy", 43).size() == 1);
}

TEST_CASE("Parse handles wasm target labels",
          "[polyui][pipeline][asm]") {
  std::string asm_text =
      "fact:\n"
      "  i32.const 1\n"
      "  i32.add\n";
  auto m = AsmModule::Parse(AsmTarget::kWasm, asm_text);
  CHECK(m.target() == AsmTarget::kWasm);
  REQUIRE(m.functions().size() == 1);
  CHECK(m.functions()[0].lines.size() == 2);
}
