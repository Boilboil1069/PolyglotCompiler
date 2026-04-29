// ============================================================================
// Unit test: legacy LINK(...) deprecation (demand 2026-04-28-8)
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;

TEST_CASE("Legacy LINK(...) form emits deprecation warning", "[ploy][link][deprec]") {
  const char *src = "LINK(cpp, ploy, cpp::foo, ploy::bar);";
  Diagnostics diags;
  PloyLexer lexer(std::string(src), "<test>");
  PloyParser parser(lexer, diags);
  parser.ParseModule();
  auto module = parser.TakeModule();
  REQUIRE(module);

  PloySema sema(diags, {});
  sema.Analyze(module);

  REQUIRE(diags.HasWarnings());
}
