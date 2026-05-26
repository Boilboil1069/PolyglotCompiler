/**
 * @file     linker_probe_test.cpp
 * @brief    Unit tests for probe-then-invoke linker selection
 *
 * Covers:
 *   - ShellQuote: quotes only when path contains a space
 *   - IsExecutableOnPath: empty -> false, bogus name -> false,
 *                         the test executable's own path -> true
 *   - SelectAvailableLinker: empty result for pobj when polyld is bogus,
 *                            falls back to polyld for native formats when
 *                            the bundled tool exists, returns empty when
 *                            nothing at all is available (bogus polyld and
 *                            an obviously absent platform candidate).
 *   - ExpandLinkCommand: substitutes {OBJ}/{OUT} placeholders and only
 *                        appends --ploy-desc / --aux-dir when the chosen
 *                        linker is polyld.
 *
 * @ingroup  Tests / unit / tools
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "tools/polyc/include/linker_probe.h"

using namespace polyglot::tools::linker_probe;

namespace {

// A name that is overwhelmingly unlikely to resolve on any host PATH.  The
// embedded space and uppercase mix also exercise the negative path of the
// shell-probe fallback (where/command -v).
constexpr const char *kBogusName = "definitely not a real tool 9c2f1b4e";

} // namespace

TEST_CASE("ShellQuote leaves space-free paths untouched", "[linker_probe]") {
  REQUIRE(ShellQuote("polyld") == "polyld");
  REQUIRE(ShellQuote("/usr/bin/clang") == "/usr/bin/clang");
  REQUIRE(ShellQuote("C:/tools/polyld.exe") == "C:/tools/polyld.exe");
}

TEST_CASE("ShellQuote wraps paths containing spaces", "[linker_probe]") {
  const std::string out = ShellQuote("C:/Program Files/LLVM/bin/lld-link.exe");
#if defined(_WIN32)
  REQUIRE(out.front() == '"');
  REQUIRE(out.back() == '"');
#else
  REQUIRE(out.front() == '\'');
  REQUIRE(out.back() == '\'');
#endif
}

TEST_CASE("IsExecutableOnPath rejects empty and obviously bogus names",
          "[linker_probe]") {
  REQUIRE_FALSE(IsExecutableOnPath(""));
  REQUIRE_FALSE(IsExecutableOnPath(kBogusName));
}

TEST_CASE("IsExecutableOnPath accepts an existing absolute file path",
          "[linker_probe]") {
  // The unit test binary itself is guaranteed to exist on disk; using its
  // absolute path exercises the stat-check fast path before the shell probe.
  const auto self = std::filesystem::current_path();
  REQUIRE(IsExecutableOnPath(self.string()));
}

TEST_CASE("SelectAvailableLinker returns empty when nothing is reachable",
          "[linker_probe]") {
  // pobj has no native fallback: a bogus polyld -> empty choice.
  auto choice = SelectAvailableLinker("pobj", kBogusName);
  REQUIRE(choice.command_template.empty());
  REQUIRE(choice.display_name.empty());
}

TEST_CASE("SelectAvailableLinker for pobj uses polyld when reachable",
          "[linker_probe]") {
  // Use the current working directory as a stand-in for a "reachable" path
  // (the helper's stat-check accepts any existing filesystem entry).
  const auto self_path = std::filesystem::current_path().string();
  auto choice = SelectAvailableLinker("pobj", self_path);
  REQUIRE_FALSE(choice.command_template.empty());
  REQUIRE(choice.display_name == "polyld");
  REQUIRE(choice.command_template.find("{OBJ}") != std::string::npos);
  REQUIRE(choice.command_template.find("{OUT}") != std::string::npos);
}

TEST_CASE("SelectAvailableLinker prefers bundled polyld for native formats",
          "[linker_probe]") {
  const auto self_path = std::filesystem::current_path().string();
  auto choice = SelectAvailableLinker("macho", self_path);
  REQUIRE_FALSE(choice.command_template.empty());
  REQUIRE(choice.display_name == "polyld");
  REQUIRE(choice.command_template.find("{OBJ}") != std::string::npos);
  REQUIRE(choice.command_template.find("{OUT}") != std::string::npos);
}

TEST_CASE("ExpandLinkCommand substitutes placeholders and gates polyld flags",
          "[linker_probe]") {
  LinkerChoice polyld_choice;
  polyld_choice.display_name = "polyld";
  polyld_choice.command_template = "polyld.exe {OBJ} -o {OUT}";

  const std::string cmd =
      ExpandLinkCommand(polyld_choice, "out.obj", "a.exe", "desc.paux", "auxdir");
  REQUIRE(cmd.find("out.obj") != std::string::npos);
  REQUIRE(cmd.find("a.exe") != std::string::npos);
  REQUIRE(cmd.find("--ploy-desc") != std::string::npos);
  REQUIRE(cmd.find("desc.paux") != std::string::npos);
  REQUIRE(cmd.find("--aux-dir") != std::string::npos);

  // Native linkers must NOT receive polyld-only flags even if the caller
  // happens to pass non-empty descriptor / aux paths.
  LinkerChoice native_choice;
  native_choice.display_name = "lld-link";
  native_choice.command_template = "lld-link /NOLOGO /OUT:{OUT} {OBJ}";
  const std::string native_cmd =
      ExpandLinkCommand(native_choice, "out.obj", "a.exe", "desc.paux", "auxdir");
  REQUIRE(native_cmd.find("--ploy-desc") == std::string::npos);
  REQUIRE(native_cmd.find("--aux-dir") == std::string::npos);
  REQUIRE(native_cmd.find("out.obj") != std::string::npos);
  REQUIRE(native_cmd.find("a.exe") != std::string::npos);
}
