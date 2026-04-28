/**
 * @file     driver.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// driver.cpp 鈥?polyc top-level driver (thin orchestration layer)
//
// This file contains only:
//   1. ParseArgs()  鈥?CLI flag parsing 鈫?DriverSettings
//   2. main()       鈥?stage orchestration: calls RunXxxStage() in order
//
// All heavy lifting has been moved into the six stage_*.cpp files.
// ============================================================================

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

#include "common/include/version.h"
#include "frontends/common/include/frontend_registry.h"
#include "backends/common/include/backend_registry.h"
#include "backends/common/include/target_backend.h"
#include "runtime/include/libs/base.h"
#include "tools/common/include/effective_settings_loader.h"
#include "tools/polyc/include/compilation_cache.h"
#include "tools/polyc/include/compilation_pipeline.h"
#include "tools/polyc/include/driver_stages.h"
#include "tools/polyc/src/stage_backend.h"
#include "tools/polyc/src/stage_bridge.h"
#include "tools/polyc/src/stage_frontend.h"
#include "tools/polyc/src/stage_marshal.h"
#include "tools/polyc/src/stage_packaging.h"
#include "tools/polyc/src/stage_semantic.h"

namespace polyglot::tools {
namespace {

namespace fs = std::filesystem;

// 鈹€鈹€ Helpers 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

std::string DetectLanguage(const std::string &path) {
  return polyglot::frontends::FrontendRegistry::Instance().DetectLanguage(path);
}

std::string ReadFileContent(const std::string &path) {
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if (!ifs.is_open())
    return {};
  auto size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  std::string content(static_cast<std::size_t>(size), '\0');
  ifs.read(content.data(), size);
  return content;
}

// Resolve a sibling tool binary (e.g. "polyld") relative to the directory
// containing the current executable (argv[0]).  Returns the bare name if
// no sibling is found so that PATH-based lookup remains the fallback.
std::string ResolveSiblingTool(const char *argv0, const std::string &tool_name) {
  if (!argv0 || !*argv0)
    return tool_name;
  std::error_code ec;
  fs::path self = fs::canonical(argv0, ec);
  if (ec)
    self = fs::path(argv0);
  fs::path candidate = self.parent_path() / tool_name;
  if (fs::exists(candidate, ec))
    return candidate.string();
  // On Windows, also try with .exe extension
#if defined(_WIN32)
  candidate = self.parent_path() / (tool_name + ".exe");
  if (fs::exists(candidate, ec))
    return candidate.string();
#endif
  return tool_name;
}

// 鈹€鈹€ ParseArgs 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

DriverSettings ParseArgs(int argc, char **argv) {
  DriverSettings s;
  bool source_set = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    // Skip the shared --settings / --print-effective-settings flags; these
    // are handled before ParseArgs() in main().
    if (arg == "--print-effective-settings") continue;
    if (arg == "--settings" && i + 1 < argc) { ++i; continue; }
    if (arg.rfind("--settings=", 0) == 0) continue;
    if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: polyc [options] <source-file-or-code>\n"
          << "\n"
          << "Options:\n"
          << "  --lang=<lang>       Language: ploy|python|cpp|rust|java|dotnet|javascript|ruby|go\n"
          << "  -O<0-3>             Optimisation level\n"
          << "  -o <output>         Output file name\n"
          << "  --mode=<mode>       compile|assemble|link\n"
          << "  --arch=<arch>       x86_64|arm64|wasm\n"
          << "  --emit-ir=<path>    Write IR to file\n"
          << "  --emit-asm=<path>   Write assembly to file\n"
          << "  --emit-obj=<path>   Write object to file\n"
          << "  --obj-format=<fmt>  pobj|coff|elf|macho\n"
          << "  --quiet             Suppress progress output\n"
          << "  --no-aux            Do not emit auxiliary files\n"
          << "  --force             Continue despite errors\n"
          << "  --strict            Reject placeholders; disable degraded stubs\n"
          << "  --dev               Dev mode: permissive + degraded fallbacks allowed\n"
          << "  --permissive        Explicit permissive override\n"
          << "  --pgo-generate      Instrument binary for profile collection\n"
          << "  --pgo-use <file>    Use profile data for guided optimisation\n"
          << "  --lto               Enable link-time optimisation\n"
          << "  --package-index     Run explicit package-index phase (default)\n"
          << "  --no-package-index  Skip package-index phase\n"
          << "  --pkg-timeout=<ms>  Per-command timeout for package indexing\n"
          << "  --progress=json     Emit machine-readable JSON progress events\n"
          << "  --clean-cache       Purge incremental compilation cache\n"
          << "  --dump-token-pool   Write <stem>.pool_stats.json with frontend pool counters\n"
          << "  -j<N>               Parallelism hint\n"
          << "  --regalloc=<mode>   linear-scan|graph-coloring\n"
          << "  --print-targets[=json|text]            List registered backends and exit\n"
          << "  --print-target-info=<triple>[:json]    Print one backend's info and exit\n"
          << "\n"
          << "External-package options:\n"
          << "  -I<path> / --I=<path>     C/C++ user header search path\n"
          << "  -isystem <path>           C/C++ system header search path\n"
          << "  -D<name>[=<value>]        Define a C/C++ preprocessor macro\n"
          << "  -U<name>                  Undefine a C/C++ preprocessor macro\n"
          << "  --python-stubs=<dir>      Add a directory of .pyi stubs\n"
          << "  --classpath=<paths> / -cp Java classpath (sep: ; on Windows, : elsewhere)\n"
          << "  --reference=<dll> / -r    .NET assembly reference (.dll / .exe)\n"
          << "  --crate-dir=<dir>         Rust cargo project root for cargo metadata\n"
          << "  --extern <name>=<path>    Rust extern crate mapping\n"
          << "  --js-project=<dir>        JavaScript/TypeScript project root (npm/yarn/pnpm)\n"
          << "  --node-modules=<dir>      Additional node_modules root for resolution\n"
          << "  --ruby-project=<dir>      Ruby Bundler project root\n"
          << "  --gem-path=<dir>          Additional gem search path (RubyGems)\n"
          << "  --go-project=<dir>        Go module root (containing go.mod)\n"
          << "  --go-mod-cache=<dir>      Additional Go module cache root\n"
          << "\n"
          << "Language version selection:\n"
          << "  --std=<dialect>           C++ dialect: c++17|c++20|c++23|c++26 (alias: -std=)\n"
          << "  --python-version=<v>      Python: 3.8|3.10|3.11|3.12|3.13\n"
          << "  --java-release=<n>        Java release: 8|11|17|21|23\n"
          << "  --cs-lang=<v>             C# language: 7.3|8|9|10|11|12\n"
          << "  --target-framework=<tfm>  .NET target: net6|net7|net8|net9\n"
          << "  --rust-edition=<y>        Rust edition: 2015|2018|2021|2024\n"
          << "  --go-version=<v>          Go release: 1.18|1.20|1.21|1.22|1.23\n"
          << "  --ecma=<v>                ECMAScript: es2017|es2020|es2022|es2023|esnext\n"
          << "  --ruby-version=<v>        Ruby: 2.7|3.0|3.2|3.3\n"
          << "  --list-language-versions  Print the supported version matrix and exit\n"
          << "\n"
          << "Source can be a file path or inline code.\n";
      std::exit(0);
    }
    if (arg == "--list-language-versions") {
      std::cout << "polyc supported language/version matrix:\n"
                << "  cpp        : c++98 c++03 c++11 c++14 c++17 c++20 c++23 c++26\n"
                << "  python     : 2.7 3.6 3.8 3.10 3.11 3.12 3.13\n"
                << "  java       : 8 11 17 21 23\n"
                << "  dotnet/cs  : 7.3 8 9 10 11 12  (target: net6 net7 net8 net9)\n"
                << "  rust       : 2015 2018 2021 2024 (editions)\n"
                << "  go         : 1.18 1.20 1.21 1.22 1.23\n"
                << "  javascript : es5 es2015 es2017 es2020 es2022 es2023 esnext\n"
                << "  ruby       : 1.9 2.7 3.0 3.2 3.3\n"
                << "Use `auto` (or omit the flag) to enable per-language inference.\n";
      std::exit(0);
    }
    if (arg.rfind("--lang=", 0) == 0) {
      s.language = arg.substr(7);
      s.language_explicit = true;
      continue;
    }
    if (arg == "--quiet" || arg == "-q") {
      s.verbose = false;
      continue;
    }
    if (arg == "--no-aux") {
      s.emit_aux = false;
      continue;
    }
    if (arg.rfind("--mode=", 0) == 0) {
      s.mode = arg.substr(7);
      continue;
    }
    if (arg.rfind("--opt=", 0) == 0) {
      auto lvl = arg.substr(6);
      if (lvl == "O1")
        s.opt_level = 1;
      else if (lvl == "O2")
        s.opt_level = 2;
      else if (lvl == "O3")
        s.opt_level = 3;
      else
        s.opt_level = 0;
      continue;
    }
    if (arg.rfind("-O", 0) == 0 && arg.size() == 3) {
      char c = arg[2];
      if (c >= '0' && c <= '3')
        s.opt_level = c - '0';
      continue;
    }
    if (arg == "-o" && i + 1 < argc) {
      s.output = argv[++i];
      continue;
    }
    if (arg.rfind("-o", 0) == 0 && arg.size() > 2) {
      s.output = arg.substr(2);
      continue;
    }
    if (arg.rfind("--emit-asm=", 0) == 0) {
      s.emit_asm_path = arg.substr(11);
      continue;
    }
    if (arg.rfind("--emit-obj=", 0) == 0) {
      s.emit_obj_path = arg.substr(11);
      continue;
    }
    if (arg.rfind("--obj-format=", 0) == 0) {
      s.obj_format = arg.substr(13);
      continue;
    }
    if (arg.rfind("--polyld=", 0) == 0) {
      s.polyld_path = arg.substr(9);
      continue;
    }
    if (arg.rfind("--emit-ir=", 0) == 0) {
      s.emit_ir_path = arg.substr(10);
      continue;
    }
    if (arg.rfind("--arch=", 0) == 0) {
      s.arch = arg.substr(7);
      continue;
    }
    if (arg == "--force" || arg == "--ignore-diagnostics") {
      s.force = true;
      continue;
    }
    if (arg == "--strict") {
      s.strict = true;
      continue;
    }
    if (arg == "--dev") {
      s.dev_mode = true;
      s.force = true;
      continue;
    }
    if (arg == "--permissive") {
      s.permissive = true;
      continue;
    }
    if (arg == "--pgo-generate") {
      s.pgo_generate = true;
      continue;
    }
    if (arg == "--pgo-use" && i + 1 < argc) {
      s.pgo_use_path = argv[++i];
      continue;
    }
    if (arg.rfind("--pgo-use=", 0) == 0) {
      s.pgo_use_path = arg.substr(10);
      continue;
    }
    if (arg == "--lto") {
      s.lto_enabled = true;
      continue;
    }
    if (arg == "--package-index") {
      s.package_index = true;
      continue;
    }
    if (arg == "--no-package-index") {
      s.package_index = false;
      continue;
    }
    if (arg == "--progress=json") {
      s.progress_json = true;
      continue;
    }
    if (arg == "--dump-token-pool") {
      s.dump_token_pool = true;
      continue;
    }
    if (arg == "--clean-cache") {
      s.clean_cache = true;
      continue;
    }
    if (arg.rfind("--pkg-timeout=", 0) == 0) {
      s.package_index_timeout_ms = std::atoi(arg.substr(14).c_str());
      if (s.package_index_timeout_ms < 0)
        s.package_index_timeout_ms = 0;
      continue;
    }
    if (arg == "-j" && i + 1 < argc) {
      s.jobs = std::atoi(argv[++i]);
      continue;
    }
    if (arg.rfind("-j", 0) == 0 && arg.size() > 2) {
      s.jobs = std::atoi(arg.substr(2).c_str());
      if (s.jobs < 1)
        s.jobs = 1;
      continue;
    }
    if (arg.rfind("--regalloc=", 0) == 0) {
      auto m = arg.substr(11);
      s.regalloc = (m == "graph" || m == "graph-coloring" || m == "coloring")
                       ? RegAllocChoice::kGraphColoring
                       : RegAllocChoice::kLinearScan;
      continue;
    }
    if (arg.rfind("--I=", 0) == 0) {
      s.include_paths.push_back(arg.substr(4));
      continue;
    }
    if (arg == "--I" && i + 1 < argc) {
      s.include_paths.push_back(argv[++i]);
      continue;
    }
    if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
      s.include_paths.push_back(arg.substr(2));
      continue;
    }
    if (arg == "-I" && i + 1 < argc) {
      s.include_paths.push_back(argv[++i]);
      continue;
    }
    if (arg == "-isystem" && i + 1 < argc) {
      s.system_include_paths.push_back(argv[++i]);
      continue;
    }
    if (arg.rfind("-isystem=", 0) == 0) {
      s.system_include_paths.push_back(arg.substr(9));
      continue;
    }
    if (arg.rfind("-D", 0) == 0 && arg.size() > 2) {
      s.defines.push_back(arg.substr(2));
      continue;
    }
    if (arg == "-D" && i + 1 < argc) {
      s.defines.push_back(argv[++i]);
      continue;
    }
    if (arg.rfind("-U", 0) == 0 && arg.size() > 2) {
      s.undefines.push_back(arg.substr(2));
      continue;
    }
    if (arg == "-U" && i + 1 < argc) {
      s.undefines.push_back(argv[++i]);
      continue;
    }
    if (arg.rfind("--python-stubs=", 0) == 0) {
      s.python_stub_paths.push_back(arg.substr(15));
      continue;
    }
    if (arg == "--python-stubs" && i + 1 < argc) {
      s.python_stub_paths.push_back(argv[++i]);
      continue;
    }
    if (arg.rfind("--classpath=", 0) == 0 || arg.rfind("-classpath=", 0) == 0) {
      std::string raw = (arg[1] == '-') ? arg.substr(12) : arg.substr(11);
#if defined(_WIN32)
      const char sep = ';';
#else
      const char sep = ':';
#endif
      std::size_t p = 0;
      while (p < raw.size()) {
        auto q = raw.find(sep, p);
        if (q == std::string::npos)
          q = raw.size();
        if (q > p)
          s.classpath.push_back(raw.substr(p, q - p));
        p = q + 1;
      }
      continue;
    }
    if ((arg == "-cp" || arg == "--classpath") && i + 1 < argc) {
      std::string raw = argv[++i];
#if defined(_WIN32)
      const char sep = ';';
#else
      const char sep = ':';
#endif
      std::size_t p = 0;
      while (p < raw.size()) {
        auto q = raw.find(sep, p);
        if (q == std::string::npos)
          q = raw.size();
        if (q > p)
          s.classpath.push_back(raw.substr(p, q - p));
        p = q + 1;
      }
      continue;
    }
    if (arg.rfind("--reference=", 0) == 0) {
      s.dotnet_references.push_back(arg.substr(12));
      continue;
    }
    if ((arg == "-r" || arg == "--reference") && i + 1 < argc) {
      s.dotnet_references.push_back(argv[++i]);
      continue;
    }
    if (arg.rfind("--crate-dir=", 0) == 0) {
      s.rust_crate_dir = arg.substr(12);
      continue;
    }
    if (arg == "--crate-dir" && i + 1 < argc) {
      s.rust_crate_dir = argv[++i];
      continue;
    }
    if (arg == "--extern" && i + 1 < argc) {
      std::string spec = argv[++i];
      auto eq = spec.find('=');
      if (eq != std::string::npos) {
        s.rust_externs.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
      } else {
        s.rust_externs.emplace_back(spec, std::string{});
      }
      continue;
    }
    if (arg.rfind("--extern=", 0) == 0) {
      std::string spec = arg.substr(9);
      auto eq = spec.find('=');
      if (eq != std::string::npos) {
        s.rust_externs.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
      } else {
        s.rust_externs.emplace_back(spec, std::string{});
      }
      continue;
    }
    // ---------------- JavaScript / TypeScript ----------------
    if (arg.rfind("--js-project=", 0) == 0) {
      s.js_project_dir = arg.substr(13);
      continue;
    }
    if (arg == "--js-project" && i + 1 < argc) {
      s.js_project_dir = argv[++i];
      continue;
    }
    if (arg.rfind("--node-modules=", 0) == 0) {
      s.node_modules_paths.push_back(arg.substr(15));
      continue;
    }
    if (arg == "--node-modules" && i + 1 < argc) {
      s.node_modules_paths.push_back(argv[++i]);
      continue;
    }
    // ---------------- Ruby ----------------
    if (arg.rfind("--ruby-project=", 0) == 0) {
      s.ruby_project_dir = arg.substr(15);
      continue;
    }
    if (arg == "--ruby-project" && i + 1 < argc) {
      s.ruby_project_dir = argv[++i];
      continue;
    }
    if (arg.rfind("--gem-path=", 0) == 0) {
      s.gem_paths.push_back(arg.substr(11));
      continue;
    }
    if (arg == "--gem-path" && i + 1 < argc) {
      s.gem_paths.push_back(argv[++i]);
      continue;
    }
    // ---------------- Go ----------------
    if (arg.rfind("--go-project=", 0) == 0) {
      s.go_project_dir = arg.substr(13);
      continue;
    }
    if (arg == "--go-project" && i + 1 < argc) {
      s.go_project_dir = argv[++i];
      continue;
    }
    if (arg.rfind("--go-mod-cache=", 0) == 0) {
      s.go_module_paths.push_back(arg.substr(15));
      continue;
    }
    if (arg == "--go-mod-cache" && i + 1 < argc) {
      s.go_module_paths.push_back(argv[++i]);
      continue;
    }
    // Language version selection.
    // Each flag accepts the canonical token plus common aliases; "auto" keeps
    // per-language inference. Unknown values fall through to a non-fatal
    // warning so the build can continue with the conservative default.
    auto parse_version_flag = [&s](std::string_view long_form, std::string_view alias_form,
                                   const std::string &arg_in, auto setter) -> int {
      const std::string lf = std::string(long_form) + "=";
      const std::string af = std::string(alias_form) + "=";
      std::string val;
      if (arg_in.rfind(lf, 0) == 0) val = arg_in.substr(lf.size());
      else if (!alias_form.empty() && arg_in.rfind(af, 0) == 0) val = arg_in.substr(af.size());
      else return 0;
      if (!setter(val)) {
        std::cerr << "[warn] unrecognised " << long_form << " value '" << val
                  << "', falling back to language default\n";
      }
      return 1;
    };
    if (parse_version_flag("--std", "-std", arg, [&](const std::string &v) {
          auto r = frontends::ParseCppDialect(v);
          if (r) { s.cpp_dialect = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--python-version", "--py", arg, [&](const std::string &v) {
          auto r = frontends::ParsePythonVersion(v);
          if (r) { s.python_version = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--java-release", "--java", arg, [&](const std::string &v) {
          auto r = frontends::ParseJavaRelease(v);
          if (r) { s.java_release = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--cs-lang", "--csharp", arg, [&](const std::string &v) {
          auto r = frontends::ParseDotnetLangVersion(v);
          if (r) { s.dotnet_lang_version = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--target-framework", "--tfm", arg, [&](const std::string &v) {
          auto r = frontends::ParseDotnetTargetFramework(v);
          if (r) { s.dotnet_target_framework = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--rust-edition", "--edition", arg, [&](const std::string &v) {
          auto r = frontends::ParseRustEdition(v);
          if (r) { s.rust_edition = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--go-version", "--go", arg, [&](const std::string &v) {
          auto r = frontends::ParseGoVersion(v);
          if (r) { s.go_version = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--ecma", "--es", arg, [&](const std::string &v) {
          auto r = frontends::ParseEcmaVersion(v);
          if (r) { s.ecma_version = *r; return true; }
          return false;
        })) continue;
    if (parse_version_flag("--ruby-version", "--ruby", arg, [&](const std::string &v) {
          auto r = frontends::ParseRubyVersion(v);
          if (r) { s.ruby_version = *r; return true; }
          return false;
        })) continue;
    if (!source_set) {
      s.source = arg;
      source_set = true;
      continue;
    }
  }

  // File source: read content + auto-detect language
  if (source_set && fs::exists(s.source)) {
    s.source_path = fs::absolute(s.source).string();
    s.source = ReadFileContent(s.source_path);
    if (s.source.empty()) {
      std::cerr << "[error] could not read file: " << s.source_path << "\n";
      std::exit(1);
    }
    if (!s.language_explicit) {
      auto det = DetectLanguage(s.source_path);
      if (!det.empty())
        s.language = det;
    }
    s.include_paths.push_back(fs::path(s.source_path).parent_path().string());
  }
  return s;
}

// 鈹€鈹€ SetupAuxDir / SourceStem 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

std::string SetupAuxDir(const DriverSettings &s) {
  if (!s.emit_aux || s.source_path.empty())
    return {};
  fs::path aux = fs::path(s.source_path).parent_path() / "aux";
  std::error_code ec;
  fs::create_directories(aux, ec);
  if (ec) {
    std::cerr << "[warn] could not create aux/: " << ec.message() << "\n";
    return {};
  }
  return aux.string();
}

std::string SourceStem(const DriverSettings &s) {
  return s.source_path.empty() ? "output" : fs::path(s.source_path).stem().string();
}

// 鈹€鈹€ Error summary (aggregated by error code) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

void PrintErrorSummary(const frontends::Diagnostics &diags, bool json_progress) {
  std::map<int, int> error_counts;
  std::map<int, std::string> first_trace;

  for (const auto &d : diags.All()) {
    if (d.severity != frontends::DiagnosticSeverity::kError)
      continue;
    int code = static_cast<int>(d.code);
    error_counts[code]++;
    if (first_trace.find(code) == first_trace.end()) {
      first_trace[code] = frontends::Diagnostics::Format(d);
    }
  }

  if (error_counts.empty())
    return;

  if (json_progress) {
    std::cout << "{\"event\":\"error_summary\",\"errors\":[";
    bool first = true;
    for (const auto &[code, count] : error_counts) {
      if (!first)
        std::cout << ",";
      std::cout << "{\"code\":" << code << ",\"count\":" << count << "}";
      first = false;
    }
    std::cout << "]}\n" << std::flush;
  } else {
    std::cerr << "\n鈹€鈹€鈹€ Error Summary 鈹€鈹€鈹€\n";
    for (const auto &[code, count] : error_counts) {
      std::cerr << "  E" << code << ": " << count << " occurrence(s)\n";
      std::cerr << "    First: " << first_trace[code] << "\n";
    }
    std::cerr << "鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€\n";
  }
}

// 鈹€鈹€ StageTimer 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

struct StageTimer {
  std::string name;
  std::chrono::high_resolution_clock::time_point start;
  bool verbose;
  bool json_progress;
  int stage_index;
  int total_stages;

  StageTimer(const std::string &n, bool v, bool json = false, int idx = 0, int total = 0) :
      name(n), start(std::chrono::high_resolution_clock::now()), verbose(v), json_progress(json),
      stage_index(idx), total_stages(total) {
    if (json_progress) {
      std::cout << "{\"event\":\"stage_start\",\"stage\":\"" << name
                << "\",\"index\":" << stage_index << ",\"total\":" << total_stages << "}\n"
                << std::flush;
    }
    if (verbose && !json_progress)
      std::cerr << "[polyc] " << name << "... " << std::flush;
  }
  double Stop() {
    double ms =
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start)
            .count();
    if (json_progress) {
      std::cout << "{\"event\":\"stage_end\",\"stage\":\"" << name
                << "\",\"elapsed_ms\":" << std::fixed << std::setprecision(1) << ms << "}\n"
                << std::flush;
    }
    if (verbose && !json_progress)
      std::cerr << "done (" << std::fixed << std::setprecision(1) << ms << "ms)\n";
    return ms;
  }
};

} // namespace
} // namespace polyglot::tools

// ============================================================================
// main
// ============================================================================

int main(int argc, char **argv) {
  using namespace polyglot::tools;

  // 鈹€鈹€ Settings.json integration (shared with polyui IDE) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  if (auto rc = polyglot::tools::common::HandleSettingsCliFlags(argc, argv);
      rc.has_value()) {
    return *rc;
  }

  // ── Backend registry introspection ──────────────────────────────────────
  // Both flags terminate the driver before any compilation work begins.
  // They surface the BackendRegistry contents either as machine-readable
  // JSON (when "=json" is appended) or as a human-readable block.
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--print-targets" || arg == "--print-targets=json" ||
        arg == "--print-targets=text") {
      const bool as_json = (arg == "--print-targets=json");
      auto infos = polyglot::backends::BackendRegistry::Instance().List();
      if (as_json) {
        std::cout << polyglot::backends::ToJson(infos) << "\n";
      } else {
        std::cout << "polyc registered code-generation backends ("
                  << infos.size() << "):\n";
        for (const auto &info : infos) {
          std::cout << polyglot::backends::ToHumanReadable(info);
          std::cout << "\n";
        }
      }
      return 0;
    }
    if (arg.rfind("--print-target-info=", 0) == 0) {
      std::string spec = arg.substr(std::string("--print-target-info=").size());
      bool as_json = false;
      auto colon = spec.find(':');
      if (colon != std::string::npos) {
        std::string suffix = spec.substr(colon + 1);
        spec = spec.substr(0, colon);
        if (suffix == "json")
          as_json = true;
      }
      std::string diag;
      auto *backend =
          polyglot::backends::BackendRegistry::Instance().FindOrDiagnose(spec, &diag);
      if (!backend) {
        std::cerr << "[polyc] " << diag << "\n";
        return 2;
      }
      auto info = polyglot::backends::MakeBackendInfo(*backend);
      if (as_json) {
        std::cout << polyglot::backends::ToJson(info) << "\n";
      } else {
        std::cout << "polyc backend '" << info.triple << "':\n"
                  << polyglot::backends::ToHumanReadable(info);
      }
      return 0;
    }
  }

  auto total_start = std::chrono::high_resolution_clock::now();
  DriverSettings settings = ParseArgs(argc, argv);

  // Handle --clean-cache: purge the incremental cache and exit
  if (settings.clean_cache) {
    namespace fs = std::filesystem;
    std::string cache_dir;
    if (!settings.source_path.empty()) {
      cache_dir = (fs::path(settings.source_path).parent_path() / ".polyc_cache").string();
    } else {
      cache_dir = ".polyc_cache";
    }
    polyglot::compilation::CompilationCache cache(cache_dir);
    cache.Clean();
    std::cerr << "[polyc] Cache cleaned: " << cache_dir << "\n";
    return 0;
  }

  // Auto-resolve polyld path relative to the current executable when the
  // user has not provided an explicit --polyld= override.
  if (settings.polyld_path == "polyld") {
    settings.polyld_path = ResolveSiblingTool(argv[0], "polyld");
  }

  // 鈹€鈹€ Mode validation 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  if (settings.mode != "compile" && settings.mode != "assemble" && settings.mode != "link") {
    std::cerr << "[error] Unknown mode: " << settings.mode << " (use compile|assemble|link)\n";
    return 1;
  }

  // 鈹€鈹€ Strict / permissive / force reconciliation 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
#ifdef POLYC_DEFAULT_STRICT
  if (!settings.permissive && !settings.strict && !settings.dev_mode)
    settings.strict = true;
#endif
  if (settings.strict && settings.permissive) {
    std::cerr << "[error] --strict and --permissive are mutually exclusive\n";
    return 1;
  }
  if (settings.strict && settings.force) {
    std::cerr << "[error] --force is not allowed in strict mode\n";
    return 1;
  }

  const bool V = settings.verbose;

  // 鈹€鈹€ Banner 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  if (V) {
    std::cerr << "========================================\n";
    std::cerr << " " << POLYGLOT_VERSION_BANNER << "  (" << POLYGLOT_POLYC_NAME << ")\n";
    std::cerr << "========================================\n";
    if (!settings.source_path.empty())
      std::cerr << "[polyc] Source: " << settings.source_path << "\n";
    std::cerr << "[polyc] Language: " << settings.language
              << (settings.language_explicit ? " (explicit)" : " (auto-detected)") << "\n";
    std::cerr << "[polyc] Arch: " << settings.arch << "\n";
    std::cerr << "[polyc] Opt: O" << settings.opt_level << "\n";
    if (settings.pgo_generate)
      std::cerr << "[polyc] PGO: generate (instrumented)\n";
    if (!settings.pgo_use_path.empty())
      std::cerr << "[polyc] PGO: use " << settings.pgo_use_path << "\n";
    if (settings.lto_enabled)
      std::cerr << "[polyc] LTO: enabled\n";
    std::cerr << "[polyc] Output: " << settings.output << "\n";
    std::cerr << "[polyc] Mode: "
              << (settings.dev_mode ? "dev"
                  : settings.strict ? "strict"
                                    : "permissive")
              << "\n";
    std::cerr << "----------------------------------------\n";
  }

  if (settings.jobs > 1 && V)
    std::cerr << "[polyc] -j" << settings.jobs << " noted (single-threaded for now)\n";

  // 鈹€鈹€ .ploy: delegate to existing CompilationPipeline 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  if (settings.language == "ploy") {
    std::string aux_dir = SetupAuxDir(settings);
    std::string source_label = settings.source_path.empty() ? "<cli>" : settings.source_path;

    polyglot::compilation::CompilationContext::Config cfg;
    cfg.source_file = settings.source_path;
    cfg.source_text = settings.source;
    cfg.source_language = settings.language;
    cfg.output_file = settings.output;
    cfg.target_arch = settings.arch;
    cfg.mode = settings.mode;
    cfg.object_format = settings.obj_format;
    cfg.polyld_path = settings.polyld_path;
    cfg.source_label = source_label;
    cfg.opt_level = settings.opt_level;
    cfg.verbose = settings.verbose;
    cfg.strict_mode = settings.strict;
    cfg.force = settings.force;
    cfg.aux_dir = aux_dir;
    cfg.package_index = settings.package_index;
    cfg.package_index_timeout_ms = settings.package_index_timeout_ms;
    cfg.emit_ir_path = settings.emit_ir_path;
    cfg.emit_asm_path = settings.emit_asm_path;
    cfg.emit_obj_path = settings.emit_obj_path;

    polyglot::compilation::CompilationPipeline pipeline(std::move(cfg));
    bool ok = false;
    {
      StageTimer t("Staged compilation pipeline (.ploy)", V, settings.progress_json, 1, 1);
      ok = pipeline.RunAll();
      t.Stop();
    }
    if (!ok) {
      std::cerr << "[error] staged pipeline failed.\n";
      for (const auto &d : pipeline.GetContext().diagnostics->All())
        std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
      return 1;
    }
    if (V) {
      for (const auto &tm : pipeline.GetContext().timings)
        std::cerr << "  - " << tm.name << ": " << std::fixed << std::setprecision(1)
                  << tm.elapsed_ms << "ms\n";
      std::cerr << "[polyc] Compilation successful (staged pipeline).\n";
    }
    return 0;
  }

  // 鈹€鈹€ Non-.ploy: six-stage pipeline 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  std::string aux_dir = SetupAuxDir(settings);
  std::string stem = SourceStem(settings);
  if (!aux_dir.empty() && V)
    std::cerr << "[polyc] Aux dir: " << aux_dir << "\n";

  // GC scratch root (required by runtime)
  void *scratch = polyglot_alloc(32);
  polyglot_gc_register_root(&scratch);

  const bool JP = settings.progress_json;
  double stage_ms[6] = {};

  // Stage 1
  FrontendResult frontend;
  {
    StageTimer t("frontend", V, JP, 1, 6);
    frontend = RunFrontendStage(settings);
    stage_ms[0] = t.Stop();
  }
  // Token-pool stats dump (--dump-token-pool).  Written before any potential
  // error short-circuit so the user always sees it when explicitly asked.
  if (settings.dump_token_pool && !frontend.token_pool_stats_json.empty() &&
      !aux_dir.empty()) {
    fs::path stats_path = fs::path(aux_dir) / (stem + ".pool_stats.json");
    std::ofstream ofs(stats_path);
    if (ofs.is_open()) {
      ofs << frontend.token_pool_stats_json;
      if (V)
        std::cerr << "[polyc] wrote " << stats_path.string() << "\n";
    }
  }
  if (!frontend.success && !settings.force) {
    std::cerr << "[error] Frontend stage failed.\n";
    for (const auto &d : frontend.diagnostics.All())
      std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
    PrintErrorSummary(frontend.diagnostics, JP);
    polyglot_gc_unregister_root(&scratch);
    return 1;
  }

  // Stage 2
  SemanticResult semantic;
  {
    StageTimer t("semantic", V, JP, 2, 6);
    semantic = RunSemanticStage(settings, frontend);
    stage_ms[1] = t.Stop();
  }
  if (!semantic.success && !settings.force) {
    std::cerr << "[error] Semantic stage failed.\n";
    for (const auto &d : semantic.diagnostics.All())
      std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
    PrintErrorSummary(semantic.diagnostics, JP);
    polyglot_gc_unregister_root(&scratch);
    return 1;
  }

  // Stage 3
  MarshalResult marshal;
  {
    StageTimer t("marshal", V, JP, 3, 6);
    marshal = RunMarshalStage(settings, semantic);
    stage_ms[2] = t.Stop();
  }

  // Stage 4
  BridgeResult bridge;
  {
    StageTimer t("bridge", V, JP, 4, 6);
    bridge = RunBridgeStage(settings, marshal, semantic, aux_dir, stem);
    stage_ms[3] = t.Stop();
  }
  if (!bridge.success && !settings.force) {
    std::cerr << "[error] Bridge stage failed.\n";
    for (const auto &d : bridge.diagnostics.All())
      std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
    PrintErrorSummary(bridge.diagnostics, JP);
    polyglot_gc_unregister_root(&scratch);
    return 1;
  }

  // Stage 5
  BackendResult backend;
  {
    StageTimer t("backend", V, JP, 5, 6);
    backend = RunBackendStage(settings, frontend, semantic, bridge);
    stage_ms[4] = t.Stop();
  }
  if (!backend.success) {
    std::cerr << "[error] Backend stage failed.\n";
    for (const auto &d : backend.diagnostics.All())
      std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
    PrintErrorSummary(backend.diagnostics, JP);
    polyglot_gc_unregister_root(&scratch);
    return 1;
  }

  // Stage 6
  PackagingResult packaging;
  {
    StageTimer t("packaging", V, JP, 6, 6);
    packaging = RunPackagingStage(settings, backend, bridge, aux_dir, stem);
    stage_ms[5] = t.Stop();
  }
  if (!packaging.success) {
    std::cerr << "[error] Packaging stage failed.\n";
    for (const auto &d : packaging.diagnostics.All())
      std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
    PrintErrorSummary(packaging.diagnostics, JP);
    polyglot_gc_unregister_root(&scratch);
    return 1;
  }

  // 鈹€鈹€ Summary 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  double total_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::high_resolution_clock::now() - total_start)
                        .count();

  // Write build_profile.bin to aux directory (binary stage statistics)
  if (!aux_dir.empty() && settings.emit_aux) {
    fs::path profile_path = fs::path(aux_dir) / "build_profile.bin";
    std::ofstream pfs(profile_path.string(), std::ios::binary);
    if (pfs.is_open()) {
      // Binary format: [uint32_t stage_count] then per-stage:
      //   [uint32_t name_len][char[] name][double elapsed_ms]
      struct StageRecord {
        std::string name;
        double ms;
      };
      StageRecord records[] = {
          {"frontend", stage_ms[0]}, {"semantic", stage_ms[1]}, {"marshal", stage_ms[2]},
          {"bridge", stage_ms[3]},   {"backend", stage_ms[4]},  {"packaging", stage_ms[5]},
      };
      uint32_t count = 6;
      pfs.write(reinterpret_cast<const char *>(&count), sizeof(count));
      for (const auto &r : records) {
        uint32_t nlen = static_cast<uint32_t>(r.name.size());
        pfs.write(reinterpret_cast<const char *>(&nlen), sizeof(nlen));
        pfs.write(r.name.data(), static_cast<std::streamsize>(nlen));
        pfs.write(reinterpret_cast<const char *>(&r.ms), sizeof(r.ms));
      }
      pfs.close();
    }
  }

  // JSON progress: completion event
  if (JP) {
    std::cout << "{\"event\":\"complete\",\"success\":true,\"total_ms\":" << std::fixed
              << std::setprecision(1) << total_ms << "}\n"
              << std::flush;
  }

  if (V) {
    std::cerr << "----------------------------------------\n";
    std::cerr << "[polyc] Target: " << backend.target_triple << "\n";
    std::cerr << "[polyc] Format: " << settings.obj_format << "\n";
    std::cerr << "[polyc] Total:  " << std::fixed << std::setprecision(1) << total_ms << "ms\n";
    if (!aux_dir.empty())
      std::cerr << "[polyc] Aux: " << aux_dir << "\n";
    std::cerr << "[polyc] Compilation successful.\n";
    std::cerr << "========================================\n";
  }

  polyglot_gc_unregister_root(&scratch);
  return 0;
}