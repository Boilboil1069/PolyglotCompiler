// ============================================================================
// Unit test: STAGE outside PIPELINE is a syntax error (demand 2026-04-28-8)
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;

TEST_CASE("STAGE outside PIPELINE produces diagnostic", "[ploy][stage]") {
  const char *src = "STAGE cpp CALL foo::bar;";
  Diagnostics diags;
  PloyLexer lexer(std::string(src), "<test>");
  PloyParser parser(lexer, diags);
  parser.ParseModule();
  // Parser should have reported an unexpected-keyword diagnostic for STAGE.
  // Wrap the disjunction in parentheses so Catch2's expression decomposer
  // accepts it as a single boolean (operator|| at REQUIRE top-level is
  // explicitly disallowed by Catch2 to avoid ambiguous decomposition).
  const bool reported = (diags.HasErrors() || diags.HasWarnings());
  REQUIRE(reported);
}
