/**
 * @file     symbol_index_test.cpp
 * @brief    Unit tests for SymbolIndex (demand 2026-04-28-22 §5)
 *
 * @ingroup  Tests / unit / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "tools/polyls/polyls_core/symbol_index.h"

using polyglot::polyls::IndexEntryKind;
using polyglot::polyls::PathToUri;
using polyglot::polyls::SymbolIndex;
using polyglot::polyls::SymbolLocation;
using polyglot::polyls::UriToPath;

namespace {

std::filesystem::path MakeTmpDir(const char *suffix) {
  auto base = std::filesystem::temp_directory_path() /
              (std::string("polyls_idx_") + suffix);
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  return base;
}

bool ContainsLine(const std::vector<SymbolLocation> &locs,
                  std::uint32_t line) {
  return std::any_of(
      locs.begin(), locs.end(),
      [line](const SymbolLocation &l) { return l.line == line; });
}

}  // namespace

TEST_CASE("SymbolIndex indexes ploy FUNC / STRUCT / LET",
          "[polyls][symbol_index]") {
  SymbolIndex idx;
  const std::string text =
      "FUNC compute(a: INT, b: INT) -> INT {\n"
      "  RETURN a + b\n"
      "}\n"
      "STRUCT Buffer { size: INT }\n"
      "LET total: INT = compute(1, 2)\n";
  idx.IndexDocument("file:///w/main.ploy", "ploy", text);
  REQUIRE(idx.DocumentCount() == 1);
  REQUIRE(idx.EntryCount() == 3);

  auto def = idx.Definition("compute");
  REQUIRE(def.size() == 1);
  REQUIRE(def[0].line == 0);
  REQUIRE(def[0].uri == "file:///w/main.ploy");

  auto buf = idx.Definition("Buffer");
  REQUIRE(buf.size() == 1);
  REQUIRE(ContainsLine(buf, 3));

  auto refs = idx.References("compute", /*include_definition=*/true);
  REQUIRE(refs.size() >= 2);  // declaration + LET initializer
}

TEST_CASE("SymbolIndex rebuilds incrementally without leaking entries",
          "[polyls][symbol_index]") {
  SymbolIndex idx;
  idx.IndexDocument("file:///w/a.ploy", "ploy",
                    "FUNC alpha() -> INT { RETURN 1 }\n");
  REQUIRE(idx.EntryCount() == 1);

  // Replace contents — alpha should disappear, beta should appear.
  idx.IndexDocument("file:///w/a.ploy", "ploy",
                    "FUNC beta() -> INT { RETURN 2 }\n");
  REQUIRE(idx.EntryCount() == 1);
  REQUIRE(idx.Definition("alpha").empty());
  REQUIRE_FALSE(idx.Definition("beta").empty());

  idx.RemoveDocument("file:///w/a.ploy");
  REQUIRE(idx.EntryCount() == 0);
  REQUIRE(idx.Definition("beta").empty());
}

TEST_CASE("SymbolIndex resolves cross-language LINK forward and backward",
          "[polyls][symbol_index]") {
  SymbolIndex idx;
  // .ploy LINK declaration, tuple form.
  idx.IndexDocument(
      "file:///w/pipe.ploy", "ploy",
      "LINK(cpp, ploy, image_processor::enhance, ploy_enhance) {\n"
      "  MAP_TYPE INT TO i32\n"
      "}\n");
  // Host-language target.
  idx.IndexDocument("file:///w/image_processor.cpp", "cpp",
                    "namespace image_processor {\n"
                    "void enhance(int x) { (void)x; }\n"
                    "}\n");

  // Forward jump: from LINK qualifier → cpp file.
  auto fwd = idx.CrossLanguageTarget("cpp", "image_processor::enhance");
  REQUIRE_FALSE(fwd.empty());
  REQUIRE(fwd.front().uri == "file:///w/image_processor.cpp");

  // Reverse: from cpp symbol → ploy LINK site.
  auto back = idx.CrossLanguageBackrefs("cpp", "image_processor::enhance");
  REQUIRE_FALSE(back.empty());
  REQUIRE(back.front().uri == "file:///w/pipe.ploy");
}

TEST_CASE("SymbolIndex round-trips through cache", "[polyls][symbol_index]") {
  const auto cache = MakeTmpDir("cache");

  SymbolIndex a;
  a.IndexDocument("file:///w/main.ploy", "ploy",
                  "FUNC compute() -> INT { RETURN 1 }\n");
  REQUIRE(a.SaveToCache(cache.string()));
  REQUIRE(std::filesystem::exists(cache / "symbol_index.json"));

  SymbolIndex b;
  REQUIRE(b.LoadFromCache(cache.string()));
  REQUIRE(b.EntryCount() == a.EntryCount());
  REQUIRE_FALSE(b.Definition("compute").empty());

  std::filesystem::remove_all(cache);
}

TEST_CASE("SymbolIndex Uri ↔ Path helpers round-trip",
          "[polyls][symbol_index]") {
  const std::string p = "/tmp/some path/with spaces.ploy";
  const std::string u = PathToUri(p);
  REQUIRE(u.rfind("file://", 0) == 0);
  // Round trip preserves the underlying path on POSIX (no percent
  // encoding is required for the input we feed in).
  REQUIRE(UriToPath(u) == p);
}
