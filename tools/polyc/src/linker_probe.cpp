/**
 * @file     linker_probe.cpp
 * @brief    Implementation of probe-then-invoke linker selection
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#include "tools/polyc/include/linker_probe.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace polyglot::tools::linker_probe {

std::string ShellQuote(const std::string &p) {
  if (p.find(' ') == std::string::npos)
    return p;
#if defined(_WIN32)
  // Windows CMD treats double quotes as the shell-quoting character; the
  // commands we spawn (link / lld-link / clang / polyld) all accept paths
  // wrapped in plain double quotes.
  return std::string{"\""} + p + "\"";
#else
  // POSIX single-quote with embedded-quote escaping (foo'bar -> 'foo'\''bar').
  std::string out{"'"};
  for (char c : p) {
    if (c == '\'')
      out += "'\\''";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
#endif
}

bool IsExecutableOnPath(const std::string &exe) {
  if (exe.empty())
    return false;

  // Direct path: stat-check first.  Covers absolute paths produced by
  // ResolveSiblingTool() in driver.cpp (e.g. the resolved sibling polyld)
  // and any explicit relative path supplied via --polyld=.
  std::error_code ec;
  if (std::filesystem::exists(exe, ec))
    return true;

#if defined(_WIN32)
  // Common Windows convenience: callers may pass "polyld" without the
  // .exe suffix when the executable sits next to polyc.
  if (std::filesystem::exists(exe + ".exe", ec))
    return true;
  // Final probe: ask the shell to resolve the bare name on PATH.
  // > NUL 2>&1 swallows both the success line and the "INFO: could not find"
  // line so the probe itself never produces visible output.
  const std::string cmd = "where " + ShellQuote(exe) + " > NUL 2>&1";
#else
  // POSIX equivalent: command -v walks PATH and returns 0 on hit.
  const std::string cmd = "command -v " + ShellQuote(exe) + " >/dev/null 2>&1";
#endif
  return std::system(cmd.c_str()) == 0;
}

namespace {

// Build the command template for a given linker name + object format.
// Placeholders {OBJ} / {OUT} are substituted by ExpandLinkCommand().
// The bundled polyld branch deliberately keeps the literal token "polyld"
// so the SelectAvailableLinker() caller can replace it with the resolved
// (potentially absolute and quoted) path.
std::string LinkerCommandTemplate(const std::string &linker, const std::string &format) {
  if (linker == "polyld") {
    // polyld dispatches by magic bytes (DetectFormat in tools/polyld
    // linker.cpp), so the same template works for pobj and all native
    // formats it can ingest.
    return "polyld {OBJ} -o {OUT}";
  }
  if (format == "coff") {
    if (linker == "link")
      return "link /NOLOGO /OUT:{OUT} {OBJ}";
    if (linker == "lld-link")
      return "lld-link /NOLOGO /OUT:{OUT} {OBJ}";
  } else if (format == "macho") {
    // -e __start: Mach-O entry symbol with the underscore prefix.
    // -undefined dynamic_lookup: tolerate foreign-language symbols that
    // resolve at final link time.
    if (linker == "clang" || linker == "ld")
      return linker + " -o {OUT} {OBJ} -Wl,-e,__start -Wl,-undefined,dynamic_lookup";
  } else { // ELF and any other unknown
    // --entry=_start: ELF/SysV entry symbol.
    // --unresolved-symbols=ignore-all: tolerate foreign-language symbols.
    if (linker == "clang" || linker == "gcc")
      return linker + " -o {OUT} {OBJ} -Wl,--entry=_start "
                      "-Wl,--unresolved-symbols=ignore-all -nostdlib";
    if (linker == "ld")
      return "ld -o {OUT} {OBJ} --entry=_start --unresolved-symbols=ignore-all";
  }
  return {};
}

LinkerChoice MakePolyldChoice(const std::string &polyld_path, const std::string &display) {
  LinkerChoice c;
  c.display_name = display;
  std::string templ = LinkerCommandTemplate("polyld", "pobj");
  // Replace the literal "polyld" token with the (possibly absolute, possibly
  // space-containing) resolved path; ShellQuote handles the spaces.
  const std::string token = "polyld";
  auto pos = templ.find(token);
  if (pos != std::string::npos)
    templ.replace(pos, token.size(), ShellQuote(polyld_path));
  c.command_template = templ;
  return c;
}

} // namespace

LinkerChoice SelectAvailableLinker(const std::string &format, const std::string &polyld_path) {
  // POBJ has only one canonical consumer.
  if (format == "pobj") {
    if (IsExecutableOnPath(polyld_path))
      return MakePolyldChoice(polyld_path, "polyld");
    return {};
  }

  std::vector<std::string> candidates;
  if (format == "coff")
    candidates = {"link", "lld-link"};
  else if (format == "macho")
    candidates = {"clang", "ld"};
  else // elf and unknowns
    candidates = {"clang", "gcc", "ld"};

  for (const auto &name : candidates) {
    if (IsExecutableOnPath(name)) {
      LinkerChoice c;
      c.display_name = name;
      c.command_template = LinkerCommandTemplate(name, format);
      if (!c.command_template.empty())
        return c;
    }
  }

  // Universal fallback: bundled polyld (it can ingest COFF / ELF / Mach-O
  // by magic-byte sniffing).
  if (IsExecutableOnPath(polyld_path))
    return MakePolyldChoice(polyld_path, "polyld (fallback)");

  return {};
}

std::string ExpandLinkCommand(const LinkerChoice &choice, const std::string &obj_path,
                              const std::string &out_path, const std::string &ploy_desc_file,
                              const std::string &aux_dir) {
  std::string cmd = choice.command_template;
  auto replace_all = [&](const std::string &needle, const std::string &value) {
    std::string::size_type pos = 0;
    while ((pos = cmd.find(needle, pos)) != std::string::npos) {
      cmd.replace(pos, needle.size(), value);
      pos += value.size();
    }
  };
  replace_all("{OBJ}", ShellQuote(obj_path));
  replace_all("{OUT}", ShellQuote(out_path));
  // --ploy-desc / --aux-dir are polyld-specific flags; only emit them when
  // the chosen linker actually understands them.
  if (choice.display_name.rfind("polyld", 0) == 0) {
    if (!ploy_desc_file.empty())
      cmd += " --ploy-desc " + ShellQuote(ploy_desc_file);
    if (!aux_dir.empty())
      cmd += " --aux-dir " + ShellQuote(aux_dir);
  }
  return cmd;
}

} // namespace polyglot::tools::linker_probe
