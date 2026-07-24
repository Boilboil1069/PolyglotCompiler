/**
 * @file native_file_builtin_test.cpp
 * @brief Poly source ABI coverage for automatically linked file input.
 */

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/statements.h"

using namespace polyglot;

namespace {

struct LoweredSource {
  frontends::Diagnostics diagnostics;
  ir::IRContext context;
};

bool Lower(const std::string &source, LoweredSource &out) {
  ploy::PloyLexer lexer(source, "<native-file-test>");
  ploy::PloyParser parser(lexer, out.diagnostics);
  parser.ParseModule();
  auto module = parser.TakeModule();
  if (!module || out.diagnostics.HasErrors())
    return false;
  ploy::PloySemaOptions options;
  options.enable_package_discovery = false;
  ploy::PloySema sema(out.diagnostics, options);
  if (!sema.Analyze(module))
    return false;
  ploy::PloyLowering lowering(out.context, out.diagnostics, sema);
  return lowering.Lower(module);
}

} // namespace

TEST_CASE("Poly file read and write builtins lower to the stable scalar polyrt ABI",
          "[ploy][native_file][lowering]") {
  LoweredSource lowered;
  REQUIRE(Lower(R"(
    FUNC main() -> INT {
      LET input_fd = file_open_ints("orders.csv");
      LET value = file_next_int(input_fd, -9999);
      LET output_fd = file_open_write("model.txt");
      LET text_written = file_write_text(output_fd, "weight=");
      LET int_written = file_write_int(output_fd, -42, 10);
      LET input_closed = file_close(input_fd);
      LET output_closed = file_close(output_fd);
      RETURN value + text_written + int_written + input_closed + output_closed;
    }
  )", lowered));
  REQUIRE_FALSE(lowered.diagnostics.HasErrors());

  bool saw_open_read = false;
  bool saw_next = false;
  bool saw_open_write = false;
  bool saw_write_text = false;
  bool saw_write_int = false;
  unsigned close_count = 0;
  for (const auto &function : lowered.context.Functions()) {
    for (const auto &block : function->blocks) {
      for (const auto &instruction : block->instructions) {
        const auto *call = dynamic_cast<const ir::CallInstruction *>(instruction.get());
        if (!call)
          continue;
        if (call->callee == "polyrt_open_read") {
          saw_open_read = true;
          REQUIRE(call->operands.size() == 1);
          CHECK(call->operands.front() == "str0");
        } else if (call->callee == "polyrt_read_i64_or") {
          saw_next = true;
          CHECK(call->operands.size() == 2);
        } else if (call->callee == "polyrt_open_write") {
          saw_open_write = true;
          REQUIRE(call->operands.size() == 1);
          CHECK(call->operands.front() == "str1");
        } else if (call->callee == "polyrt_write_text") {
          saw_write_text = true;
          REQUIRE(call->operands.size() == 2);
          CHECK(call->operands[1] == "str2");
        } else if (call->callee == "polyrt_write_i64") {
          saw_write_int = true;
          CHECK(call->operands.size() == 3);
        } else if (call->callee == "polyrt_close_read") {
          ++close_count;
          CHECK(call->operands.size() == 1);
        }
      }
    }
  }
  CHECK(saw_open_read);
  CHECK(saw_next);
  CHECK(saw_open_write);
  CHECK(saw_write_text);
  CHECK(saw_write_int);
  CHECK(close_count == 2);
}

TEST_CASE("Poly file builtins reject non-scalar ABI arguments",
          "[ploy][native_file][sema]") {
  const std::vector<std::string> invalid_calls = {
      "file_next_int(\"not-an-fd\", -1)",
      "file_open_write(7)",
      "file_write_text(1, 9)",
      "file_write_int(1, \"not-an-int\", 10)",
      "file_write_int(1, 9, \"not-a-separator\")",
  };
  for (const auto &invalid_call : invalid_calls) {
    INFO("invalid call: " << invalid_call);
    LoweredSource lowered;
    const std::string source =
        "FUNC main() -> INT { RETURN " + invalid_call + "; }";
    CHECK_FALSE(Lower(source, lowered));
    CHECK(lowered.diagnostics.HasErrors());
  }
}
