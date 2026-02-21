#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "common/include/ir/ir_parser.h"
#include "common/include/ir/ir_printer.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/verifier.h"

using namespace polyglot::ir;

TEST_CASE("IR round-trip with new ops and align", "[ir][parser][printer]") {
  const std::string text = R"(func test(a, b)
entry:
  x = add a, b : i32
  y = shl x, a : i32
  z = sdiv y, b : i32
  cmp = cmpslt z, a : i1
  p = alloca : i32*
  ld = load p align 4 : i32
  store p, x align 4 : void
  g = gep p [0] inbounds : i32*
  memcpy p, p, 4 align 4 : void
  memset p, 0, 4 align 4 : void
  ccall = call callee(y, z) [fn i32 (i32, i32) vararg] : i32
  ret ld : i32
)";

  IRContext ctx;
  std::string err;
  REQUIRE(ParseFunction(text, ctx, nullptr, &err));
  REQUIRE(err.empty());

  auto &fn = *ctx.Functions().back();
  bool ok = Verify(fn, &ctx.Layout(), &err);
  if (!ok) WARN("Verify: " << err);
  REQUIRE(ok);

  const std::string dumped = Dump(fn);
  REQUIRE(dumped == text);
}

TEST_CASE("Parser rejects missing type suffix", "[ir][parser][error]") {
  const std::string bad = R"(func bad()
entry:
  x = add a, b
)";
  IRContext ctx;
  std::string err;
  REQUIRE_FALSE(ParseFunction(bad, ctx, nullptr, &err));
}
