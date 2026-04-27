/**
 * @file     polyver.cpp
 * @brief    Multi-language tool-chain manager (`polyver` CLI).
 *
 * Sub-commands:
 *   polyver list   <lang>           - list discovered tool-chains for a language
 *   polyver list                    - list every discovered tool-chain
 *   polyver detect                  - probe the host and update the user catalog
 *   polyver use    <lang> <version> - pin a per-project default tool-chain
 *   polyver path   <lang> <version> - print the absolute executable path
 *   polyver        --version        - print polyver version
 *   polyver        --help           - print usage
 *
 * @ingroup  Tool / polyver
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "tools/polyver/include/toolchain_db.h"

namespace fs = std::filesystem;
using namespace polyglot::polyver;

namespace {

constexpr const char *kPolyverVersion = "1.2.0";

void PrintUsage(std::ostream &out) {
  out << "polyver " << kPolyverVersion
      << " - PolyglotCompiler tool-chain manager\n\n"
         "USAGE:\n"
         "  polyver list [<lang>]                List discovered tool-chains\n"
         "  polyver detect                       Probe the host and refresh ~/.polyglot/toolchains.json\n"
         "  polyver use   <lang> <version>       Pin a per-project default (writes .polyglot/toolchains.lock)\n"
         "  polyver path  <lang> <version>       Print the absolute executable path of a tool-chain\n"
         "  polyver --version                    Print polyver version\n"
         "  polyver --help                       Print this help text\n\n"
         "Recognized languages: cpp, python, java, dotnet, rust, go, javascript, ruby\n";
}

int CmdList(const std::vector<std::string> &args) {
  std::vector<std::string> errors;
  auto user = ToolchainDb::LoadFromFile(ToolchainDb::UserCatalogPath(), errors);
  for (const auto &e : errors)
    std::cerr << e << "\n";

  std::string filter = args.empty() ? std::string{} : args[0];
  std::printf("%-12s %-10s %-9s %s\n", "LANGUAGE", "VERSION", "DEFAULT", "PATH");
  for (const auto &e : user.Entries()) {
    if (!filter.empty() && e.language != filter)
      continue;
    std::printf("%-12s %-10s %-9s %s\n", e.language.c_str(), e.version.c_str(),
                e.is_default ? "yes" : "no", e.path.c_str());
  }
  return 0;
}

int CmdDetect() {
  std::vector<std::string> errors;
  auto user      = ToolchainDb::LoadFromFile(ToolchainDb::UserCatalogPath(), errors);
  auto detected  = DetectToolchains();
  MergeToolchainDb(user, detected);

  if (!user.SaveToFile(ToolchainDb::UserCatalogPath())) {
    std::cerr << "polyver: failed to write " << ToolchainDb::UserCatalogPath() << "\n";
    return 1;
  }
  std::cout << "polyver: discovered " << detected.Entries().size()
            << " tool-chain(s); catalog at " << ToolchainDb::UserCatalogPath() << "\n";
  return 0;
}

int CmdUse(const std::vector<std::string> &args) {
  if (args.size() < 2) {
    std::cerr << "polyver use: missing <lang> <version>\n";
    return 2;
  }
  const std::string &lang = args[0];
  const std::string &ver  = args[1];

  fs::path project = ToolchainDb::FindProjectRoot(fs::current_path());
  if (project.empty()) {
    // Bootstrap a .polyglot/ folder in the current directory so that
    // future calls find this as the project root.
    project = fs::current_path();
    fs::create_directories(project / ".polyglot");
  }

  std::vector<std::string> errors;
  auto                     lock = ToolchainDb::LoadFromFile(ToolchainDb::ProjectLockPath(project),
                                                            errors);

  // Inject an entry from the user catalog if available so that `path`
  // queries can succeed without re-detection.
  auto user = ToolchainDb::LoadFromFile(ToolchainDb::UserCatalogPath(), errors);
  if (auto found = user.Find(lang, ver))
    lock.AddEntry(*found);
  else {
    // Still allow pinning; record an entry without an executable path.
    ToolchainEntry e;
    e.language = lang;
    e.version  = ver;
    e.vendor   = "user-pinned";
    lock.AddEntry(e);
  }
  if (!lock.SetDefault(lang, ver)) {
    std::cerr << "polyver: failed to set default after insertion\n";
    return 1;
  }
  if (!lock.SaveToFile(ToolchainDb::ProjectLockPath(project))) {
    std::cerr << "polyver: cannot write " << ToolchainDb::ProjectLockPath(project) << "\n";
    return 1;
  }
  std::cout << "polyver: pinned " << lang << "=" << ver << " in "
            << ToolchainDb::ProjectLockPath(project) << "\n";
  return 0;
}

int CmdPath(const std::vector<std::string> &args) {
  if (args.size() < 2) {
    std::cerr << "polyver path: missing <lang> <version>\n";
    return 2;
  }
  const std::string &lang = args[0];
  const std::string &ver  = args[1];

  std::vector<std::string> errors;
  fs::path                 project = ToolchainDb::FindProjectRoot(fs::current_path());
  if (!project.empty()) {
    auto lock = ToolchainDb::LoadFromFile(ToolchainDb::ProjectLockPath(project), errors);
    if (auto e = lock.Find(lang, ver); e && !e->path.empty()) {
      std::cout << e->path << "\n";
      return 0;
    }
  }
  auto user = ToolchainDb::LoadFromFile(ToolchainDb::UserCatalogPath(), errors);
  if (auto e = user.Find(lang, ver); e && !e->path.empty()) {
    std::cout << e->path << "\n";
    return 0;
  }
  std::cerr << "polyver: " << lang << "=" << ver
            << " not found in any catalog (run `polyver detect` first)\n";
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i)
    args.emplace_back(argv[i]);

  if (args.empty() || args[0] == "--help" || args[0] == "-h") {
    PrintUsage(std::cout);
    return 0;
  }
  if (args[0] == "--version" || args[0] == "-V") {
    std::cout << kPolyverVersion << "\n";
    return 0;
  }

  const std::string                cmd = args[0];
  const std::vector<std::string>   rest(args.begin() + 1, args.end());
  if (cmd == "list")
    return CmdList(rest);
  if (cmd == "detect")
    return CmdDetect();
  if (cmd == "use")
    return CmdUse(rest);
  if (cmd == "path")
    return CmdPath(rest);

  std::cerr << "polyver: unknown sub-command '" << cmd << "'\n";
  PrintUsage(std::cerr);
  return 2;
}
