/**
 * @file     explicit_widths_e2e_test.cpp
 * @brief    End-to-end integration test for explicit-width primitive types,
 *           TYPE aliases, and CONST declarations introduced by demand
 *           2026-04-28-7.  Drives a small but realistic .ploy program that
 *           combines all three features through the lexer + parser + sema
 *           pipeline and checks the resolved tables.
 *
 * @ingroup  Tests / integration / explicit_widths
 * @author   Manning Cyrus
 * @date     2026-04-29
 */

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::core::TypeKind;
using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

TEST_CASE(
    "End-to-end: TYPE alias + CONST + explicit widths flow through the ploy "
    "frontend into a populated sema state",
    "[integration][ploy][explicit_widths]") {
  // The program below mirrors the shape of the new
  // tests/samples/31_explicit_widths showcase but is kept inline so the
  // test runs in any working directory without needing fixture files.
  // It exercises:
  //   * TYPE alias for a width-aware integer (`Pixel = i32`).
  //   * TYPE alias for a parameterised type (`Buffer = LIST(u8)`).
  //   * Three CONST declarations whose initializers exercise: literal,
  //     reference to a previously declared CONST, and arithmetic on
  //     literals.
  //   * A FUNC parameter typed via the alias to ensure substitution
  //     reaches the function-signature path.
  static const char *kSource =
      "type Pixel = i32;\n"
      "type Buffer = LIST(u8);\n"
      "const KMaxRetry: i32 = 5;\n"
      "const KDoubleRetry: i32 = KMaxRetry;\n"
      "const KArea: i64 = 10 * 20;\n"
      "func render(p: Pixel) -> i64 { return KArea; }\n";

  Diagnostics diags;
  PloyLexer lexer(std::string(kSource), "<explicit-widths-e2e>");
  PloyParser parser(lexer, diags);
  parser.ParseModule();
  auto module = parser.TakeModule();
  REQUIRE(module);

  PloySema sema(diags, PloySemaOptions{});
  sema.Analyze(module);
  REQUIRE_FALSE(diags.HasErrors());

  // Type alias table populated with both aliases.
  const auto &aliases = sema.TypeAliases();
  REQUIRE(aliases.count("Pixel") == 1);
  REQUIRE(aliases.count("Buffer") == 1);

  CHECK(aliases.at("Pixel").kind == TypeKind::kInt);
  CHECK(aliases.at("Pixel").bit_width == 32);
  CHECK(aliases.at("Pixel").is_signed);

  CHECK(aliases.at("Buffer").kind == TypeKind::kArray);
  REQUIRE(aliases.at("Buffer").type_args.size() == 1);
  CHECK(aliases.at("Buffer").type_args[0].kind == TypeKind::kInt);
  CHECK(aliases.at("Buffer").type_args[0].bit_width == 8);
  CHECK_FALSE(aliases.at("Buffer").type_args[0].is_signed);

  // Constant table populated for all three CONSTs.
  REQUIRE(sema.ConstantCount() == 3);
  REQUIRE(sema.LookupConstantText("KMaxRetry") != nullptr);
  REQUIRE(sema.LookupConstantText("KDoubleRetry") != nullptr);
  REQUIRE(sema.LookupConstantText("KArea") != nullptr);
  CHECK(*sema.LookupConstantText("KMaxRetry") == "5");
  // Constant-propagation step: KDoubleRetry inherits the literal 5.
  CHECK(*sema.LookupConstantText("KDoubleRetry") == "5");
  // KArea preserves the operator + operands so the IR stage can re-fold.
  CHECK(sema.LookupConstantText("KArea")->find("10") != std::string::npos);
  CHECK(sema.LookupConstantText("KArea")->find("20") != std::string::npos);

  // Function signature picked up the alias substitution.
  const auto &sigs = sema.KnownSignatures();
  auto sig_it = sigs.find("render");
  REQUIRE(sig_it != sigs.end());
  REQUIRE(sig_it->second.param_count == 1);
  REQUIRE_FALSE(sig_it->second.param_types.empty());
  CHECK(sig_it->second.param_types[0].kind == TypeKind::kInt);
  CHECK(sig_it->second.param_types[0].bit_width == 32);
  CHECK(sig_it->second.return_type.kind == TypeKind::kInt);
  CHECK(sig_it->second.return_type.bit_width == 64);

  // CONST symbols are surfaced in the regular symbol table as immutable
  // variables — the IDE's identifier-completion path consumes them
  // through the same query that handles LET-bindings.
  const auto &symbols = sema.Symbols();
  REQUIRE(symbols.count("KMaxRetry") == 1);
  CHECK_FALSE(symbols.at("KMaxRetry").is_mutable);
}
