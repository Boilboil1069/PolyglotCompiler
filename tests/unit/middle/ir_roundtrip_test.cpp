#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include "middle/include/ir/ir_parser.h"
#include "middle/include/ir/ir_printer.h"
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
  REQUIRE(Verify(fn, &ctx.Layout(), &err));
  REQUIRE(err.empty());

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

TEST_CASE("Integer to float casts survive IR roundtrip and validate types",
          "[ir][parser][printer][numeric-cast]") {
  const std::string text = R"(func convert()
entry:
  value = const_bits 9007199254740993 : i64
  signed_float = sitofp value : f32
  unsigned_double = uitofp value : f64
  ret unsigned_double : f64
)";
  IRContext ctx;
  std::string error;
  REQUIRE(ParseFunction(text, ctx, nullptr, &error));
  const auto &function = *ctx.Functions().back();
  CHECK(Dump(function) == text);
  REQUIRE(Verify(function, &ctx.Layout(), &error));
  auto cast = dynamic_cast<CastInstruction *>(function.blocks[0]->instructions[1].get());
  REQUIRE(cast != nullptr);
  cast->type = IRType::I32();
  CHECK_FALSE(Verify(function, &ctx.Layout(), &error));
  CHECK(error.find("require integer source and float destination") != std::string::npos);
}
