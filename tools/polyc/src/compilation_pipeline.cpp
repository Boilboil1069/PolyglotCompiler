#include "tools/polyc/include/native_entry.h"
/**
 * @file     compilation_pipeline.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/passes/opt.h"
#include "middle/include/ir/ssa.h"
#include "middle/include/ir/verifier.h"
#include "middle/include/passes/pass_manager.h"
#include "middle/include/passes/transform/instrument_call_trace.h"

#include "backends/arm64/include/arm64_target.h"
#include "backends/common/include/backend_registry.h"
#include "backends/common/include/object_file.h"
#include "backends/common/include/target_backend.h"
#include "backends/wasm/include/wasm_target.h"
#include "backends/x86_64/include/x86_target.h"
#include "frontends/common/include/frontend_registry.h"
#include "frontends/ploy/include/package_indexer.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "runtime/include/libs/native_file_runtime.h"
#include "tools/polyc/include/compilation_pipeline.h"
#include "tools/polyc/include/linker_probe.h"
#include "tools/polyc/include/native_call_trace.h"
#include "tools/polyc/src/foreign_signature_extractor.h"
#include "tools/polyc/src/local_source_packages.h"

namespace polyglot::compilation {
namespace {

namespace fs = std::filesystem;

using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using polyglot::tools::linker_probe::ExpandLinkCommand;
using polyglot::tools::linker_probe::LinkerChoice;
using polyglot::tools::linker_probe::SelectAvailableLinker;

constexpr std::uint32_t kPobjSectionFlagBss = 1u << 1;

bool UsesNativeFileRuntime(const ir::IRContext &ctx) {
  for (const auto &fn : ctx.Functions()) {
    if (!fn)
      continue;
    for (const auto &block : fn->blocks) {
      if (!block)
        continue;
      for (const auto &instruction : block->instructions) {
        const auto *call = dynamic_cast<const ir::CallInstruction *>(instruction.get());
        if (call && runtime::IsNativeFileRuntimeSymbol(call->callee))
          return true;
      }
    }
  }
  return false;
}

std::string RuntimeTargetOS(const CompilationContext::Config &config) {
  if (!config.target_os.empty())
    return config.target_os;
  using common::OS;
  switch (config.target_triple.os) {
  case OS::kDarwin:
    return "darwin";
  case OS::kLinux:
    return "linux";
  case OS::kWindows:
    return "windows";
  case OS::kFreeBSD:
    return "freebsd";
  case OS::kWasi:
    return "wasi";
  case OS::kNone:
    return "none";
  case OS::kUnknown:
    return "unknown";
  }
  return "unknown";
}

std::string CanonicalSourceLanguage(std::string language) {
  std::string folded = language;
  std::transform(folded.begin(), folded.end(), folded.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (folded == "poly" || folded == "ploy")
    return "poly";
  return language;
}

std::string AbiLanguageToken(const std::string &language) {
  const std::string canonical = CanonicalSourceLanguage(language);
  return canonical == "poly" ? "ploy" : canonical;
}

void AppendUniqueDirectory(std::vector<std::string> &paths, const fs::path &candidate) {
  std::error_code ec;
  if (!fs::is_directory(candidate, ec))
    return;

  fs::path normalized = fs::weakly_canonical(candidate, ec);
  if (ec)
    normalized = fs::absolute(candidate, ec);
  const std::string value = normalized.lexically_normal().string();
  if (value.empty())
    return;
  if (std::find(paths.begin(), paths.end(), value) == paths.end())
    paths.push_back(value);
}

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string TrimAscii(std::string value) {
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(),
                           [&](unsigned char c) { return !is_space(c); }));
  value.erase(std::find_if(value.rbegin(), value.rend(),
                           [&](unsigned char c) { return !is_space(c); })
                  .base(),
              value.end());
  return value;
}

// Parse the small, deliberately constrained string subset used by
// poly.package.toml metadata.  Requiring a TOML string (instead of accepting
// arbitrary bare values) keeps paths and package identities unambiguous.
std::optional<std::string> ParseManifestString(std::string value) {
  value = TrimAscii(std::move(value));
  if (value.size() < 2)
    return std::nullopt;

  const char quote = value.front();
  if ((quote != '"' && quote != '\'') || value.back() != quote)
    return std::nullopt;

  std::string result;
  result.reserve(value.size() - 2);
  for (std::size_t i = 1; i + 1 < value.size(); ++i) {
    const char c = value[i];
    if (quote == '"' && c == '\\') {
      if (++i + 1 >= value.size())
        return std::nullopt;
      const char escaped = value[i];
      if (escaped != '\\' && escaped != '"')
        return std::nullopt;
      result.push_back(escaped);
      continue;
    }
    if (c == quote)
      return std::nullopt;
    result.push_back(c);
  }
  return result;
}

struct LocalPackageManifest {
  std::string name;
  std::string language;
  std::string include_dir;
};

std::optional<LocalPackageManifest> ReadLocalPackageManifest(const fs::path &manifest_path) {
  std::ifstream input(manifest_path);
  if (!input)
    return std::nullopt;

  LocalPackageManifest manifest;
  std::string line;
  while (std::getline(input, line)) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;
    std::size_t comment = std::string::npos;
    for (std::size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\' && in_double_quote) {
        escaped = true;
        continue;
      }
      if (c == '\'' && !in_double_quote)
        in_single_quote = !in_single_quote;
      else if (c == '"' && !in_single_quote)
        in_double_quote = !in_double_quote;
      else if (c == '#' && !in_single_quote && !in_double_quote) {
        comment = i;
        break;
      }
    }
    if (comment != std::string::npos)
      line.erase(comment);
    line = TrimAscii(std::move(line));
    if (line.empty())
      continue;
    // Package identity fields belong to the TOML root table.  Do not let an
    // equally named field in [exports] or another table override them.
    if (line.front() == '[')
      break;

    const auto equals = line.find('=');
    if (equals == std::string::npos)
      continue;
    const std::string key = TrimAscii(line.substr(0, equals));
    auto value = ParseManifestString(line.substr(equals + 1));
    if (!value)
      continue;
    if (key == "name")
      manifest.name = std::move(*value);
    else if (key == "language")
      manifest.language = LowerAscii(std::move(*value));
    else if (key == "include_dir")
      manifest.include_dir = std::move(*value);
  }

  if (manifest.name.empty() || manifest.language.empty() || manifest.include_dir.empty())
    return std::nullopt;
  return manifest;
}

std::set<std::string> DiscoverDeclaredCppPackages(const CompilationContext::Config &config) {
  std::string source = config.source_text;
  if (source.empty() && !config.source_file.empty()) {
    std::ifstream input(config.source_file);
    if (input)
      source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }

  std::vector<frontends::Token> tokens;
  ploy::PloyLexer lexer(std::move(source), config.source_file);
  for (;;) {
    auto token = lexer.NextToken();
    const bool done = token.kind == frontends::TokenKind::kEndOfFile;
    tokens.push_back(std::move(token));
    if (done)
      break;
  }

  std::set<std::string> packages;
  for (std::size_t i = 0; i + 3 < tokens.size(); ++i) {
    if (tokens[i].kind != frontends::TokenKind::kKeyword ||
        tokens[i].lexeme != "IMPORT")
      continue;
    if ((tokens[i + 1].kind != frontends::TokenKind::kIdentifier &&
         tokens[i + 1].kind != frontends::TokenKind::kKeyword) ||
        LowerAscii(tokens[i + 1].lexeme) != "cpp")
      continue;
    if (tokens[i + 2].kind != frontends::TokenKind::kKeyword ||
        tokens[i + 2].lexeme != "PACKAGE" ||
        tokens[i + 3].kind != frontends::TokenKind::kIdentifier)
      continue;

    std::string name = tokens[i + 3].lexeme;
    std::size_t cursor = i + 4;
    while (cursor + 1 < tokens.size() &&
           tokens[cursor].kind == frontends::TokenKind::kSymbol &&
           tokens[cursor].lexeme == "." &&
           tokens[cursor + 1].kind == frontends::TokenKind::kIdentifier) {
      name += "." + tokens[cursor + 1].lexeme;
      cursor += 2;
    }
    packages.insert(std::move(name));
  }
  return packages;
}

bool IsSafePackageRelativePath(const fs::path &path) {
  if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    return false;
  return std::none_of(path.begin(), path.end(),
                      [](const fs::path &part) { return part == ".."; });
}

bool IsPathWithin(const fs::path &child, const fs::path &parent) {
  const auto mismatch = std::mismatch(parent.begin(), parent.end(), child.begin(), child.end());
  return mismatch.first == parent.end();
}

// A .poly entry is also the project root for hermetic, vendored native
// packages. Project-wide include/ and vendor/include conventions remain
// available, while package include roots are admitted only when the Poly file
// declares `IMPORT cpp PACKAGE <name>` and a matching poly.package.toml names
// both that package and the C++ language. Explicit CLI include paths retain
// precedence because discovered paths are appended afterwards.
void DiscoverProjectIncludePaths(CompilationContext::Config &config) {
  if (config.source_language != "poly" || config.source_file.empty())
    return;

  std::error_code ec;
  fs::path source = fs::absolute(config.source_file, ec);
  if (ec)
    source = fs::path(config.source_file);
  const fs::path root = source.parent_path();

  AppendUniqueDirectory(config.include_paths, root / "include");
  AppendUniqueDirectory(config.include_paths, root / "vendor" / "include");

  const auto declared_packages = DiscoverDeclaredCppPackages(config);
  if (declared_packages.empty())
    return;

  const fs::path packages = root / "packages";
  if (!fs::is_directory(packages, ec))
    return;

  std::vector<fs::path> package_roots;
  for (fs::directory_iterator it(packages, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_directory(ec))
      package_roots.push_back(it->path());
  }
  std::sort(package_roots.begin(), package_roots.end());
  for (const auto &package_root : package_roots) {
    const auto manifest = ReadLocalPackageManifest(package_root / "poly.package.toml");
    if (!manifest || manifest->language != "cpp" ||
        declared_packages.find(manifest->name) == declared_packages.end())
      continue;

    const fs::path declared_include = fs::path(manifest->include_dir);
    if (!IsSafePackageRelativePath(declared_include))
      continue;
    const fs::path relative_include = declared_include.lexically_normal();

    std::error_code canonical_ec;
    const fs::path canonical_root = fs::weakly_canonical(package_root, canonical_ec);
    if (canonical_ec)
      continue;
    const fs::path canonical_include =
        fs::weakly_canonical(package_root / relative_include, canonical_ec);
    if (canonical_ec || !IsPathWithin(canonical_include, canonical_root))
      continue;
    AppendUniqueDirectory(config.include_paths, canonical_include);
  }
}

#pragma pack(push, 1)
struct PobjFileHeader {
  char magic[4];
  std::uint16_t version{1};
  std::uint16_t section_count{0};
  std::uint16_t symbol_count{0};
  std::uint16_t reloc_count{0};
  std::uint64_t strtab_offset{0};
};

struct PobjSectionRecord {
  std::uint32_t name_offset{0};
  std::uint32_t flags{0};
  std::uint64_t offset{0};
  std::uint64_t size{0};
};

struct PobjSymbolRecord {
  std::uint32_t name_offset{0};
  std::uint32_t section_index{0xFFFFFFFF};
  std::uint64_t value{0};
  std::uint64_t size{0};
  std::uint8_t binding{0};
  std::uint8_t reserved[3]{};
};

struct PobjRelocRecord {
  std::uint32_t section_index{0};
  std::uint64_t offset{0};
  std::uint32_t type{0};
  std::uint32_t symbol_index{0};
  std::int64_t addend{0};
};
#pragma pack(pop)

struct InternalSection {
  std::string name;
  std::vector<std::uint8_t> data;
  bool bss{false};
  std::vector<linker::Relocation> relocs;
};

struct InternalSymbol {
  std::string name;
  std::uint32_t section_index{0xFFFFFFFF};
  std::uint64_t value{0};
  std::uint64_t size{0};
  bool global{true};
  bool defined{false};
};

// PE-7-D: result of folding a single CompiledObject's section bytes into the
// running per-name section table. We hand it back to the symbol / relocation
// passes so they can shift their offsets by `base_offset` (the size the
// merged section had right before this object's bytes were appended).
struct ObjAbsorbInfo {
  std::string canonical_name;
  std::uint32_t section_index{0xFFFFFFFF};
  std::uint64_t base_offset{0};
};

// PE-7-D: merge one CompiledObject's bytes into the per-name section table.
// Each CompiledObject corresponds to exactly one MC section (the backend's
// `absorb_artifacts` path materialises one per emitted section), so this
// function picks `obj.code` for `.text` and `obj.data` for everything else,
// then either appends to the existing same-name InternalSection or registers
// a fresh entry. The selector-style `sec.data = !obj.code.empty() ? ... :
// obj.data;` shortcut that used to live here silently dropped any non-`.text`
// payload of objects whose `obj.code` happened to be non-empty (none today,
// but a footgun for any future backend that mixes both fields in one object).
ObjAbsorbInfo AbsorbObjectSections(const CompiledObject &obj,
                                   std::vector<InternalSection> &sections,
                                   std::unordered_map<std::string, std::uint32_t> &sec_index) {
  ObjAbsorbInfo info;
  info.canonical_name = obj.name.empty() ? std::string(".text") : obj.name;
  const auto &payload = (info.canonical_name == ".text" && !obj.code.empty()) ? obj.code
                                                                              : obj.data;

  auto it = sec_index.find(info.canonical_name);
  if (it == sec_index.end()) {
    InternalSection sec;
    sec.name = info.canonical_name;
    sec.data = payload;
    info.section_index = static_cast<std::uint32_t>(sections.size());
    info.base_offset = 0;
    sec_index[info.canonical_name] = info.section_index;
    sections.push_back(std::move(sec));
  } else {
    info.section_index = it->second;
    auto &dst = sections[info.section_index];
    info.base_offset = static_cast<std::uint64_t>(dst.data.size());
    dst.data.insert(dst.data.end(), payload.begin(), payload.end());
  }
  return info;
}

// PE-7-D: stable section ordering used by the packaging pass. The ABI-fixed
// loaders downstream (polyld + the native COFF/ELF/Mach-O writers) all expect
// `.text` first, then read-only data, then writable data, then BSS. Anything
// else keeps its original first-seen order so backends that emit custom
// sections stay deterministic.
int SectionPriority(const std::string &name) {
  if (name == ".text") return 0;
  if (name == ".rdata" || name == ".rodata") return 1;
  if (name == ".data") return 2;
  if (name == ".bss") return 3;
  return 4;
}

void AppendDiagnostics(frontends::Diagnostics &src, std::vector<frontends::Diagnostic> &dst) {
  const auto &all = src.All();
  dst.insert(dst.end(), all.begin(), all.end());
}

std::string ReadTextFile(const std::string &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open())
    return {};
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

// Return the default native object format string for the current host.
std::string HostObjectFormat() {
#if defined(_WIN32)
  return "coff";
#elif defined(__APPLE__)
  return "macho";
#else
  return "elf";
#endif
}

// Build a native-format object file (ELF/Mach-O) from internal sections and
// symbols using the backend ObjectFileBuilder API.  Returns the file content
// as a byte vector, or an empty vector on failure.
std::vector<std::uint8_t> BuildNativeObjectBinary(const std::string &format,
                                                  const std::string &arch,
                                                  const std::vector<InternalSection> &sections,
                                                  const std::vector<InternalSymbol> &symbols) {
  bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
  std::unique_ptr<backends::ObjectFileBuilder> builder;
  if (format == "macho") {
    builder = std::make_unique<backends::MachOBuilder>(is_arm64);
  } else if (format == "coff") {
    // Windows / PE-style COFF translation unit.
    builder = std::make_unique<backends::COFFBuilder>(is_arm64);
  } else if (format == "elf") {
    builder = std::make_unique<backends::ELFBuilder>(!is_arm64);
  } else {
    // Unknown format string: refuse rather than silently mis-emitting.
    return {};
  }

  for (const auto &sec : sections) {
    backends::Section bs;
    bs.name = sec.name;
    bs.data = sec.data;
    for (const auto &r : sec.relocs) {
      backends::Relocation br;
      br.offset = r.offset;
      br.symbol = r.symbol;
      br.type = r.type;
      br.addend = r.addend;
      bs.relocations.push_back(std::move(br));
    }
    builder->AddSection(bs);
  }
  for (const auto &sym : symbols) {
    backends::Symbol bs;
    bs.name = sym.name;
    bs.section = (sym.section_index < sections.size()) ? sections[sym.section_index].name : "";
    bs.offset = sym.value;
    bs.size = sym.size;
    bs.is_global = sym.global;
    bs.is_function = true;
    builder->AddSymbol(bs);
  }
  return builder->Build();
}

// Determine object file extension for the given format string.
std::string ObjectExtension(const std::string &fmt) {
  if (fmt == "pobj")
    return ".pobj";
  if (fmt == "coff")
    return ".obj";
  return ".o";
}

// Probe-then-invoke linker discovery is shared with stage_packaging.cpp via
// the helpers in tools/polyc/include/linker_probe.h.  See that header for
// the selection rules and command-template contract.

std::vector<std::uint8_t> BuildPobjBinary(const std::vector<InternalSection> &sections,
                                          const std::vector<InternalSymbol> &symbols) {
  std::vector<std::uint8_t> strtab{0};
  auto add_str = [&](const std::string &s) {
    std::uint32_t off = static_cast<std::uint32_t>(strtab.size());
    strtab.insert(strtab.end(), s.begin(), s.end());
    strtab.push_back(0);
    return off;
  };

  std::vector<PobjSectionRecord> sec_records;
  sec_records.reserve(sections.size());
  for (const auto &s : sections) {
    PobjSectionRecord rec{};
    rec.name_offset = add_str(s.name);
    rec.flags = s.bss ? kPobjSectionFlagBss : 0;
    rec.size = static_cast<std::uint64_t>(s.data.size());
    sec_records.push_back(rec);
  }

  std::vector<PobjSymbolRecord> sym_records;
  sym_records.reserve(symbols.size());
  for (const auto &s : symbols) {
    PobjSymbolRecord rec{};
    rec.name_offset = add_str(s.name);
    rec.section_index = s.defined ? s.section_index : 0xFFFFFFFF;
    rec.value = s.value;
    rec.size = s.size;
    rec.binding = s.global ? 1 : 0;
    sym_records.push_back(rec);
  }

  std::vector<PobjRelocRecord> reloc_records;
  for (std::size_t si = 0; si < sections.size(); ++si) {
    for (const auto &r : sections[si].relocs) {
      PobjRelocRecord rr{};
      rr.section_index = static_cast<std::uint32_t>(si);
      rr.offset = r.offset;
      rr.type = r.type;
      rr.symbol_index = (r.symbol_index >= 0) ? static_cast<std::uint32_t>(r.symbol_index) : 0;
      rr.addend = r.addend;
      reloc_records.push_back(rr);
    }
  }

  std::size_t cursor = sizeof(PobjFileHeader) + sec_records.size() * sizeof(PobjSectionRecord) +
                       sym_records.size() * sizeof(PobjSymbolRecord) +
                       reloc_records.size() * sizeof(PobjRelocRecord);

  for (std::size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].bss) {
      sec_records[i].offset = 0;
    } else {
      sec_records[i].offset = cursor;
      cursor += sections[i].data.size();
    }
  }

  PobjFileHeader hdr{};
  std::memcpy(hdr.magic, "POBJ", 4);
  hdr.version = 1;
  hdr.section_count = static_cast<std::uint16_t>(sec_records.size());
  hdr.symbol_count = static_cast<std::uint16_t>(sym_records.size());
  hdr.reloc_count = static_cast<std::uint16_t>(reloc_records.size());
  hdr.strtab_offset = cursor;

  std::vector<std::uint8_t> out;
  out.resize(cursor + strtab.size());

  std::size_t w = 0;
  auto write_bytes = [&](const void *ptr, std::size_t n) {
    std::memcpy(out.data() + w, ptr, n);
    w += n;
  };

  write_bytes(&hdr, sizeof(hdr));
  if (!sec_records.empty()) {
    write_bytes(sec_records.data(), sec_records.size() * sizeof(PobjSectionRecord));
  }
  if (!sym_records.empty()) {
    write_bytes(sym_records.data(), sym_records.size() * sizeof(PobjSymbolRecord));
  }
  if (!reloc_records.empty()) {
    write_bytes(reloc_records.data(), reloc_records.size() * sizeof(PobjRelocRecord));
  }

  for (const auto &s : sections) {
    if (s.bss || s.data.empty())
      continue;
    write_bytes(s.data.data(), s.data.size());
  }

  write_bytes(strtab.data(), strtab.size());
  return out;
}

std::string ShellQuoteArg(const std::string &arg) {
  return polyglot::tools::linker_probe::ShellQuote(arg);
}

std::string JoinCommandArgs(const std::vector<std::string> &args) {
  std::ostringstream oss;
  bool first = true;
  for (const auto &arg : args) {
    if (arg.empty())
      continue;
    if (!first)
      oss << ' ';
    first = false;
    oss << ShellQuoteArg(arg);
  }
  return oss.str();
}

std::string ResolveSelfPolyc() {
  std::error_code ec;
  fs::path self = fs::canonical("/proc/self/exe", ec);
#if defined(__APPLE__)
  if (ec) {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
      buffer.resize(std::strlen(buffer.c_str()));
      self = fs::canonical(buffer, ec);
      if (ec)
        self = fs::path(buffer);
    } else {
      self.clear();
    }
  }
#endif
  return self.empty() ? std::string{"polyc"} : self.string();
}

std::string SanitizedSymbol(const std::string &name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name)
    out.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
  return out;
}

std::string ModuleFromQualified(const std::string &qualified) {
  auto pos = qualified.rfind("::");
  if (pos == std::string::npos || pos == 0)
    return {};
  return qualified.substr(0, pos);
}

std::string FunctionFromQualified(const std::string &qualified) {
  auto pos = qualified.rfind("::");
  return pos == std::string::npos ? qualified : qualified.substr(pos + 2);
}

std::vector<std::string> ModulePathCandidates(const std::string &module_name) {
  std::vector<std::string> out{module_name};
  std::string nested = module_name;
  std::string::size_type pos = 0;
  while ((pos = nested.find("::", pos)) != std::string::npos) {
    nested.replace(pos, 2, 1, fs::path::preferred_separator);
    ++pos;
  }
  if (nested != module_name)
    out.push_back(nested);
  return out;
}

std::string ResolveImportedSourceFile(const CompilationContext::Config &config,
                                      const std::string &language,
                                      const std::string &module_name) {
  const auto *frontend = frontends::FrontendRegistry::Instance().GetFrontend(language);
  if (!frontend)
    return {};

  std::vector<fs::path> roots;
  if (!config.source_file.empty())
    roots.push_back(fs::path(config.source_file).parent_path());
  for (const auto &inc : config.include_paths)
    roots.push_back(fs::path(inc));

  std::error_code ec;
  for (const auto &root : roots) {
    for (const auto &module_path : ModulePathCandidates(module_name)) {
      for (const auto &prefix : {fs::path{}, fs::path(language)}) {
        for (const auto &ext : frontend->Extensions()) {
          fs::path candidate = root / prefix / module_path;
          candidate += ext;
          if (fs::exists(candidate, ec))
            return candidate.string();
        }
      }
    }
  }
  return {};
}

struct ImportedFunction {
  std::string language;
  std::string module;
  std::string function;
  std::string qualified;
  std::string source_file;
};

std::vector<ImportedFunction> CollectImportedFunctions(const CompilationContext::Config &config) {
  std::vector<ImportedFunction> result;
  std::ifstream in(config.ploy_desc_file);
  if (!in.is_open())
    return result;

  std::unordered_set<std::string> seen;
  std::string op;
  while (in >> op) {
    if (op == "IMPORT") {
      std::string language;
      std::string module;
      in >> language >> module;
      const std::string key = language + "::" + module + "::*";
      if (!seen.insert(key).second)
        continue;
      const std::string source = ResolveImportedSourceFile(config, language, module);
      if (!source.empty())
        result.push_back(ImportedFunction{language, module, "", "", source});
    } else if (op == "SYMBOL") {
      std::string name;
      std::string language;
      std::string mangled;
      in >> name >> language >> mangled;
      if (language == "poly")
        continue;
      const std::string module = ModuleFromQualified(name);
      const std::string function = FunctionFromQualified(name);
      if (module.empty() || function.empty())
        continue;
      const std::string key = language + "::" + module + "::" + function;
      if (!seen.insert(key).second)
        continue;
      const std::string source = ResolveImportedSourceFile(config, language, module);
      if (!source.empty())
        result.push_back(ImportedFunction{language, module, function, name, source});
    } else {
      std::string rest;
      std::getline(in, rest);
    }
  }
  return result;
}

std::string CompileImportedSource(const CompilationContext::Config &config,
                                  const ImportedFunction &fn,
                                  const std::string &effective_fmt) {
  const auto module_started = std::chrono::steady_clock::now();
  if (config.aux_dir.empty())
    return {};
  std::string bundle_error;
  const std::string compilation_source = ::polyglot::tools::BuildVendoredSourceBundle(
      config.source_file, fn.language, fn.source_file, config.aux_dir, &bundle_error,
      !config.package_index);
  if (compilation_source.empty()) {
    if (config.verbose && !bundle_error.empty())
      std::cerr << "[pipeline] " << bundle_error << "\n";
    return {};
  }
  fs::path out = fs::path(config.aux_dir) /
                 (SanitizedSymbol(fn.language + "_" + fn.module) +
                  ObjectExtension(effective_fmt));
  std::vector<std::string> args = {
      ResolveSelfPolyc(),
      "--lang=" + fn.language,
      "-c",
      compilation_source,
      "-o",
      out.string(),
      "--arch=" + config.target_arch,
      "--obj-format=" + effective_fmt,
      "--target=" + config.target_triple.str(),
      "-O" + std::to_string(config.opt_level),
      "--no-aux",
  };
  switch (config.reg_alloc) {
  case backends::RegAllocStrategy::kLinearScan:
    args.push_back("--regalloc=linear-scan");
    break;
  case backends::RegAllocStrategy::kGraphColoring:
    args.push_back("--regalloc=graph-coloring");
    break;
  case backends::RegAllocStrategy::kStack:
    args.push_back("--regalloc=stack");
    break;
  }
  if (!config.verbose)
    args.push_back("--quiet");
  if (config.strict_mode)
    args.push_back("--strict");
  if (!config.package_index)
    args.push_back("--no-package-index");
  for (const auto &inc : config.include_paths)
    args.push_back("-I" + inc);
  for (const auto &inc : config.system_include_paths)
    args.push_back("-isystem=" + inc);
  for (const auto &define : config.defines)
    args.push_back("-D" + define);
  for (const auto &undefine : config.undefines)
    args.push_back("-U" + undefine);
  for (const auto &stub : config.python_stub_paths)
    args.push_back("--python-stubs=" + stub);
  if (!config.rust_crate_dir.empty())
    args.push_back("--crate-dir=" + config.rust_crate_dir);
  for (const auto &[name, path] : config.rust_externs)
    args.push_back("--extern=" + name + (path.empty() ? "" : "=" + path));
  const auto &frontend_options = config.frontend_options;
  if (!frontend_options.go_project_dir.empty())
    args.push_back("--go-project=" + frontend_options.go_project_dir);
  for (const auto &path : frontend_options.go_module_paths)
    args.push_back("--go-mod-cache=" + path);
  if (fn.language == "cpp" &&
      frontend_options.cpp_dialect != frontends::CppDialect::kAuto)
    args.push_back("--std=" +
                   std::string(frontends::CppDialectToString(frontend_options.cpp_dialect)));
  if (fn.language == "python" &&
      frontend_options.python_version != frontends::PythonVersion::kAuto)
    args.push_back("--python-version=" + std::string(frontends::PythonVersionToString(
                                               frontend_options.python_version)));
  if (fn.language == "rust" &&
      frontend_options.rust_edition != frontends::RustEdition::kAuto)
    args.push_back("--rust-edition=" + std::string(frontends::RustEditionToString(
                                             frontend_options.rust_edition)));
  if (fn.language == "go" && frontend_options.go_version != frontends::GoVersion::kAuto)
    args.push_back("--go-version=" +
                   std::string(frontends::GoVersionToString(frontend_options.go_version)));

  const std::string cmd = JoinCommandArgs(args);
  if (config.verbose) {
    std::cerr << "[pipeline] compiling import " << fn.language << "::" << fn.module
              << (compilation_source == fn.source_file ? "" : " with vendored packages")
              << " -> " << out.string() << "\n";
  }
  int rc = std::system(cmd.c_str());
  const bool produced_object = rc == 0 && fs::is_regular_file(out);
  config.module_builds.push_back(
      {fn.language, fn.source_file, out.string(),
       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - module_started)
           .count(),
       produced_object});
  if (rc != 0)
    return {};
  std::error_code ec;
  return fs::exists(out, ec) ? out.string() : std::string{};
}

linker::Relocation MakeAliasReloc(std::uint64_t offset, std::uint32_t symbol_index,
                                  const CompilationContext::Config &config) {
  linker::Relocation reloc{};
  reloc.symbol_index = static_cast<int>(symbol_index);
  reloc.addend = 0;
  const bool use_arm64 =
      (config.target_arch == "arm64" || config.target_arch == "aarch64" ||
       config.target_arch == "armv8");
  if (use_arm64) {
    reloc.offset = offset;
    reloc.type = static_cast<std::uint32_t>(linker::RelocationType_ARM64::kR_AARCH64_JUMP26);
  } else {
    reloc.offset = offset + 1; // skip the E9 jump opcode; patch rel32
    reloc.type = static_cast<std::uint32_t>(linker::RelocationType_x86_64::kR_X86_64_PLT32);
    reloc.addend = -4;
  }
  return reloc;
}

std::vector<std::uint8_t> MakeAliasCode(const CompilationContext::Config &config) {
  const bool use_arm64 =
      (config.target_arch == "arm64" || config.target_arch == "aarch64" ||
       config.target_arch == "armv8");
  // Aliases preserve the original return address and argument stack. A BL
  // followed by RET would overwrite x30 and return to its own RET forever.
  if (use_arm64)
    return {0x00, 0x00, 0x00, 0x14};
  return {0xE9, 0x00, 0x00, 0x00, 0x00};
}

void AddAliasWrapper(const CompilationContext::Config &config,
                     const std::vector<std::string> &aliases,
                     const std::string &target_symbol, InternalSection &text,
                     std::vector<InternalSymbol> &symbols) {
  std::uint32_t target_index = static_cast<std::uint32_t>(symbols.size());
  symbols.push_back(InternalSymbol{target_symbol, 0xFFFFFFFF, 0, 0, true, false});

  const auto code = MakeAliasCode(config);
  const std::uint64_t base = static_cast<std::uint64_t>(text.data.size());
  text.data.insert(text.data.end(), code.begin(), code.end());
  auto reloc = MakeAliasReloc(base, target_index, config);
  reloc.symbol = target_symbol;
  text.relocs.push_back(reloc);

  for (const auto &alias : aliases)
    symbols.push_back(InternalSymbol{alias, 0, base, static_cast<std::uint64_t>(code.size()), true,
                                     true});
}

std::string BuildImportedAliasObject(const CompilationContext::Config &config,
                                     const std::vector<ImportedFunction> &imports,
                                     const std::string &effective_fmt,
                                     const std::string &stem) {
  if (imports.empty() || config.aux_dir.empty())
    return {};

  InternalSection text;
  text.name = ".text";
  std::vector<InternalSymbol> symbols;
  std::set<std::string> emitted;

  for (const auto &fn : imports) {
    if (fn.function.empty())
      continue;
    std::string implementation = fn.function;
    if (fn.language == "java") {
      const auto separator = fn.module.rfind("::");
      const std::string class_name =
          separator == std::string::npos ? fn.module : fn.module.substr(separator + 2);
      implementation = class_name + "::" + fn.function;
    }
    const std::string target =
        (effective_fmt == "macho") ? "_" + implementation : implementation;
    std::vector<std::string> aliases;
    auto add_alias = [&](const std::string &alias) {
      if (alias != target && emitted.insert(alias).second)
        aliases.push_back(alias);
    };
    add_alias(fn.qualified);
    if (effective_fmt == "macho")
      add_alias("_" + fn.qualified);
    add_alias(SanitizedSymbol(fn.qualified));
    add_alias("_" + SanitizedSymbol(fn.qualified));
    AddAliasWrapper(config, aliases, target, text, symbols);
  }

  fs::path out = fs::path(config.aux_dir) / (stem + "_foreign_aliases.pobj");
  auto data = BuildPobjBinary(std::vector<InternalSection>{text}, symbols);
  std::ofstream ofs(out, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open())
    return {};
  ofs.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
  return ofs.good() ? out.string() : std::string{};
}

std::vector<std::string> BuildForeignInputs(const CompilationContext::Config &config,
                                            const std::string &effective_fmt,
                                            const std::string &stem,
                                            frontends::Diagnostics &diagnostics) {
  std::vector<std::string> inputs;
  if (config.ploy_desc_file.empty() || config.mode != "link")
    return inputs;
  const auto imports = CollectImportedFunctions(config);
  if (imports.empty())
    return inputs;

  std::unordered_map<std::string, std::string> compiled;
  for (const auto &fn : imports) {
    auto it = compiled.find(fn.source_file);
    if (it == compiled.end()) {
      std::string obj = CompileImportedSource(config, fn, effective_fmt);
      it = compiled.emplace(fn.source_file, obj).first;
      if (obj.empty()) {
        diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                frontends::ErrorCode::kUnresolvedSymbol,
                                "failed to auto-compile imported source: " + fn.source_file);
        continue;
      }
    }
    if (it->second.empty())
      continue;
    if (std::find(inputs.begin(), inputs.end(), it->second) == inputs.end())
      inputs.push_back(it->second);
  }

  std::string aliases = BuildImportedAliasObject(config, imports, effective_fmt, stem);
  if (!aliases.empty())
    inputs.push_back(aliases);
  return inputs;
}

class DefaultFrontendStage final : public FrontendStage {
public:
  FrontendOutput Run(const CompilationContext::Config &config,
                     frontends::Diagnostics &diagnostics) override {
    FrontendOutput out;
    out.source_file = config.source_file;
    out.language = config.source_language;

    if (config.source_language != "poly") {
      diagnostics.ReportError(
          core::SourceLoc{config.source_label.empty() ? config.source_file : config.source_label, 1,
                          1},
          frontends::ErrorCode::kInvalidLanguage,
          "staged compilation pipeline currently accepts only '.poly' sources");
      AppendDiagnostics(diagnostics, out.parse_diagnostics);
      out.success = false;
      return out;
    }

    std::string source = config.source_text;
    if (source.empty() && !config.source_file.empty()) {
      source = ReadTextFile(config.source_file);
    }
    if (source.empty()) {
      diagnostics.ReportError(
          core::SourceLoc{config.source_label.empty() ? config.source_file : config.source_label, 1,
                          1},
          frontends::ErrorCode::kMissingExpression, "source text is empty");
      AppendDiagnostics(diagnostics, out.parse_diagnostics);
      out.success = false;
      return out;
    }

    const std::string label =
        config.source_label.empty()
            ? (config.source_file.empty() ? std::string{"<memory>"} : config.source_file)
            : config.source_label;

    {
      ploy::PloyLexer token_lexer(source, label);
      while (true) {
        auto tk = token_lexer.NextToken();
        if (tk.kind == frontends::TokenKind::kEndOfFile)
          break;
        out.tokens.push_back(std::move(tk));
      }
    }

    ploy::PloyLexer parse_lexer(source, label);
    ploy::PloyParser parser(parse_lexer, diagnostics);
    parser.ParseModule();
    out.ast = parser.TakeModule();

    AppendDiagnostics(diagnostics, out.parse_diagnostics);
    out.success = (out.ast != nullptr) && !diagnostics.HasErrors();
    if (!config.source_file.empty() && std::filesystem::exists(config.source_file)) {
      out.source_mtime = std::filesystem::last_write_time(config.source_file);
    }
    return out;
  }
};

class DefaultSemanticStage final : public SemanticStage {
public:
  explicit DefaultSemanticStage(bool strict_mode = false,
                                ::polyglot::tools::ForeignExtractionOptions foreign_options = {}) :
      strict_mode_(strict_mode), foreign_options_(std::move(foreign_options)) {}

  SemanticDatabase Run(const FrontendOutput &input, frontends::Diagnostics &diagnostics,
                       const std::shared_ptr<ploy::PackageDiscoveryCache> &cache) override {
    SemanticDatabase db;
    db.validated_ast = input.ast;
    if (!input.success || !input.ast) {
      diagnostics.ReportError(
          core::SourceLoc{input.source_file.empty() ? "<memory>" : input.source_file, 1, 1},
          frontends::ErrorCode::kUnexpectedToken,
          "semantic stage requires a successfully parsed AST");
      AppendDiagnostics(diagnostics, db.sema_diagnostics);
      db.success = false;
      return db;
    }

    ploy::PloySemaOptions opts;
    opts.strict_mode = strict_mode_;
    opts.enable_package_discovery = false;
    opts.discovery_cache = cache;
    db.sema_instance = std::make_shared<ploy::PloySema>(diagnostics, opts);

    auto extraction_options = foreign_options_;
    extraction_options.diagnostics = &diagnostics;
    ::polyglot::tools::ForeignSignatureExtractor extractor(extraction_options);
    db.sema_instance->InjectForeignSignatures(extractor.ExtractAll(*input.ast));

    db.success = db.sema_instance->Analyze(input.ast);

    db.symbols = db.sema_instance->Symbols();
    db.signatures = db.sema_instance->KnownSignatures();
    db.class_schemas = db.sema_instance->ClassSchemas();
    db.link_entries = db.sema_instance->Links();
    db.type_mappings = db.sema_instance->TypeMappings();
    db.packages = db.sema_instance->DiscoveredPackages();
    db.venv_configs = db.sema_instance->VenvConfigs();

    AppendDiagnostics(diagnostics, db.sema_diagnostics);
    return db;
  }

private:
  bool strict_mode_{false};
  ::polyglot::tools::ForeignExtractionOptions foreign_options_;
};

class DefaultMarshalPlanStage final : public MarshalPlanStage {
public:
  MarshalPlan Run(const SemanticDatabase &input, frontends::Diagnostics &diagnostics) override {
    MarshalPlan plan;
    if (!input.success) {
      diagnostics.ReportError(core::SourceLoc{"<marshal>", 1, 1},
                              frontends::ErrorCode::kTypeMismatch,
                              "marshal planning requires successful semantic database");
      AppendDiagnostics(diagnostics, plan.plan_diagnostics);
      plan.success = false;
      return plan;
    }

    for (const auto &entry : input.link_entries) {
      CallMarshalPlan call_plan;
      call_plan.link_id = entry.target_symbol + "<-" + entry.source_symbol;
      call_plan.target_language = entry.target_language;
      call_plan.source_language = entry.source_language;
      call_plan.target_function = entry.target_symbol;
      call_plan.source_function = entry.source_symbol;
      call_plan.lang_version = entry.lang_version;

      const auto sig_it = input.signatures.find(entry.target_symbol);
      if (sig_it != input.signatures.end()) {
        for (std::size_t i = 0; i < sig_it->second.param_types.size(); ++i) {
          ParamMarshalPlan p;
          p.param_index = i;
          p.source_type = sig_it->second.param_types[i];
          p.target_type = sig_it->second.param_types[i];
          p.strategy = MarshalStrategy::kDirectCopy;
          p.size_bytes = 8;
          p.alignment = 8;
          call_plan.param_plans.push_back(std::move(p));
        }
        call_plan.return_plan.source_type = sig_it->second.return_type;
        call_plan.return_plan.target_type = sig_it->second.return_type;
        call_plan.return_plan.strategy = MarshalStrategy::kDirectCopy;
        call_plan.return_plan.size_bytes = 8;
      }

      call_plan.target_abi.calling_convention = "sysv64";
      call_plan.target_abi.pointer_size = 8;
      call_plan.target_abi.stack_alignment = 16;
      call_plan.target_abi.int_reg_count = 6;
      call_plan.target_abi.float_reg_count = 8;
      call_plan.source_abi = call_plan.target_abi;
      call_plan.needs_calling_conv_adapt = false;

      plan.call_plans.push_back(std::move(call_plan));
    }

    // Local source IMPORT calls do not require users to duplicate every
    // function signature in a LINK declaration.  Sema has already validated
    // them against signatures extracted from the actual imported files, so
    // carry them forward as native-import plans for automatic compilation and
    // direct common-ABI linking.
    if (input.sema_instance) {
      std::unordered_set<std::string> seen_native_calls;
      for (const auto &call : input.sema_instance->CrossLanguageCalls()) {
        if (!call || !input.sema_instance->IsLocalSourceCall(call->language, call->function))
          continue;
        const std::string key = call->language + "::" + call->function;
        if (!seen_native_calls.insert(key).second)
          continue;

        CallMarshalPlan call_plan;
        call_plan.link_id = "native-import:" + key;
        call_plan.target_language = "poly";
        call_plan.source_language = call->language;
        call_plan.target_function = call->function;
        call_plan.source_function = call->function;
        call_plan.lang_version = call->lang_version_pin;
        call_plan.is_native_import = true;

        auto sig_it = input.signatures.find(call->function);
        if (sig_it == input.signatures.end()) {
          const auto pos = call->function.rfind("::");
          if (pos != std::string::npos)
            sig_it = input.signatures.find(call->function.substr(pos + 2));
        }
        if (sig_it != input.signatures.end()) {
          for (std::size_t i = 0; i < sig_it->second.param_types.size(); ++i) {
            ParamMarshalPlan p;
            p.param_index = i;
            p.source_type = sig_it->second.param_types[i];
            p.target_type = sig_it->second.param_types[i];
            p.strategy = MarshalStrategy::kDirectCopy;
            p.size_bytes = 8;
            p.alignment = 8;
            call_plan.param_plans.push_back(std::move(p));
          }
          call_plan.return_plan.source_type = sig_it->second.return_type;
          call_plan.return_plan.target_type = sig_it->second.return_type;
          call_plan.return_plan.strategy = MarshalStrategy::kDirectCopy;
          call_plan.return_plan.size_bytes = 8;
        }

        call_plan.target_abi.calling_convention = "sysv64";
        call_plan.source_abi = call_plan.target_abi;
        plan.call_plans.push_back(std::move(call_plan));
      }
    }

    plan.success = true;
    return plan;
  }
};

class DefaultBridgeGenerationStage final : public BridgeGenerationStage {
public:
  BridgeGenerationOutput Run(const MarshalPlan &plan, const SemanticDatabase &sema_db,
                             frontends::Diagnostics &diagnostics) override {
    BridgeGenerationOutput out;
    if (!plan.success || !sema_db.success || !sema_db.sema_instance) {
      diagnostics.ReportError(
          core::SourceLoc{"<bridge>", 1, 1}, frontends::ErrorCode::kABIIncompatible,
          "bridge generation requires successful marshal plan and semantic database");
      AppendDiagnostics(diagnostics, out.generation_diagnostics);
      out.success = false;
      return out;
    }

    linker::LinkerConfig cfg;
    cfg.verbose = false;
    linker::PolyglotLinker linker(cfg);

    // Build descriptors from marshal plan.
    for (const auto &cp : plan.call_plans) {
      if (cp.is_native_import)
        continue;
      ploy::CrossLangCallDescriptor desc;
      // Mirror the mangling rule from `MangleStubName` in poly lowering:
      // weave a `_v<sanitized_version>_` segment into the stub name when a
      // foreign-language version is pinned, so that the linker can route to
      // the matching versioned bridge variant.
      desc.stub_name = "__ploy_bridge_" + AbiLanguageToken(cp.target_language) + "_" +
                       AbiLanguageToken(cp.source_language) + "_";
      if (!cp.lang_version.empty()) {
        desc.stub_name += "v";
        for (char c : cp.lang_version) {
          desc.stub_name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        }
        desc.stub_name.push_back('_');
      }
      desc.stub_name += cp.target_function;
      desc.source_language = cp.source_language;
      desc.target_language = cp.target_language;
      desc.source_function = cp.source_function;
      desc.target_function = cp.target_function;
      for ([[maybe_unused]] const auto &pm : cp.param_plans) {
        desc.source_param_types.push_back(ir::IRType::Pointer(ir::IRType::Void()));
        desc.target_param_types.push_back(ir::IRType::Pointer(ir::IRType::Void()));
        ploy::CrossLangCallDescriptor::MarshalOp op;
        op.kind = ploy::CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
        op.from = ir::IRType::Pointer(ir::IRType::Void());
        op.to = ir::IRType::Pointer(ir::IRType::Void());
        desc.param_marshal.push_back(op);
      }
      desc.source_return_type = ir::IRType::Pointer(ir::IRType::Void());
      desc.target_return_type = ir::IRType::Pointer(ir::IRType::Void());
      desc.return_marshal.kind = ploy::CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
      desc.return_marshal.from = desc.source_return_type;
      desc.return_marshal.to = desc.target_return_type;
      desc.lang_version = cp.lang_version;
      linker.AddCallDescriptor(desc);
    }

    for (const auto &entry : sema_db.link_entries) {
      linker.AddLinkEntry(entry);
    }

    std::unordered_set<std::string> registered;
    auto add_symbol = [&](const std::string &name, const std::string &lang) {
      const std::string key = lang + "::" + name;
      if (registered.count(key) != 0)
        return;
      registered.insert(key);

      linker::CrossLangSymbol s;
      s.name = name;
      s.mangled_name = name;
      s.language = lang;
      s.type = linker::SymbolType::kFunction;

      const auto sig_it = sema_db.signatures.find(name);
      if (sig_it != sema_db.signatures.end()) {
        for (const auto &t : sig_it->second.param_types) {
          linker::CrossLangSymbol::ParamDesc p;
          p.type_name = t.ToString();
          p.size = 8;
          p.is_pointer = t.IsPointer();
          s.params.push_back(std::move(p));
        }
        s.return_desc.type_name = sig_it->second.return_type.ToString();
        s.return_desc.size = 8;
        s.return_desc.is_pointer = sig_it->second.return_type.IsPointer();
      }
      linker.AddCrossLangSymbol(s);
    };

    for (const auto &entry : sema_db.link_entries) {
      add_symbol(entry.target_symbol, entry.target_language);
      add_symbol(entry.source_symbol, entry.source_language);
    }

    if (!linker.ResolveLinks()) {
      for (const auto &err : linker.GetErrors()) {
        diagnostics.ReportError(core::SourceLoc{"<bridge>", 1, 1},
                                frontends::ErrorCode::kUnresolvedSymbol, err);
      }
      AppendDiagnostics(diagnostics, out.generation_diagnostics);
      out.success = false;
      return out;
    }

    for (const auto &stub : linker.GetStubs()) {
      GeneratedStub g;
      g.stub_name = stub.stub_name;
      g.link_id = stub.target_function + "<-" + stub.source_function;
      g.code = stub.code;
      g.relocations = stub.relocations;
      g.target_symbol = stub.target_function;
      g.source_symbol = stub.source_function;
      out.stubs.push_back(std::move(g));
    }

    out.success = true;
    return out;
  }
};

class DefaultBackendStage final : public BackendStage {
public:
  BackendOutput Run(const SemanticDatabase &sema_db, const BridgeGenerationOutput &bridges,
                    const CompilationContext::Config &config,
                    frontends::Diagnostics &diagnostics) override {
    BackendOutput out;
    out.target_arch = config.target_arch;
    out.target_os = config.target_os;

    if (!sema_db.success || !sema_db.validated_ast || !sema_db.sema_instance) {
      diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                              frontends::ErrorCode::kLoweringUndefined,
                              "backend stage requires a successful semantic database");
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    auto ir_module = std::make_shared<ir::IRContext>();
    ploy::PloyLowering lowering(*ir_module, diagnostics, *sema_db.sema_instance);
    if (!lowering.Lower(sema_db.validated_ast)) {
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    // Inject resolved bridge stubs into IR before backend emission.
    for (const auto &stub : bridges.stubs) {
      auto fn = ir_module->CreateFunction(stub.stub_name);
      fn->is_external = false;
      fn->is_bridge_stub = true;
      fn->precompiled_code = stub.code;
      for (const auto &rel : stub.relocations) {
        ir::StubRelocation sr;
        sr.offset = static_cast<std::size_t>(rel.offset);
        sr.symbol = rel.symbol;
        sr.type = rel.type;
        sr.addend = rel.addend;
        sr.is_pc_relative = rel.is_pc_relative;
        sr.size = static_cast<std::uint8_t>(rel.size);
        fn->precompiled_relocs.push_back(sr);
      }
    }

    if (config.mode == "link" && config.target_arch != "wasm" &&
        config.target_arch != "wasm32" && config.target_arch != "wasm64" &&
        !tools::PrepareNativeEntry(*ir_module, "poly", config.entry_symbol, diagnostics)) {
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    // Preserve the historical __ploy_rt_* hook ABI while publishing the
    // canonical language identifier in the hook payload.
    if (!config.trace_calls_path.empty()) {
      std::string trace_error;
      const bool supported =
          (config.target_arch == "arm64" || config.target_arch == "aarch64") &&
          (RuntimeTargetOS(config) == "darwin" || RuntimeTargetOS(config) == "macos" ||
           RuntimeTargetOS(config) == "linux") &&
          config.mode == "link";
      if (!supported ||
          !tools::InstrumentNativeCallTrace(*ir_module, config.trace_calls_path, &trace_error)) {
        diagnostics.ReportError(
            core::SourceLoc{"<trace>", 1, 1}, frontends::ErrorCode::kUnsupportedLowering,
            supported ? trace_error
                      : "--trace-calls requires a native ARM64 Linux/macOS Poly executable");
        AppendDiagnostics(diagnostics, out.backend_diagnostics);
        out.success = false;
        return out;
      }
    }
    if (config.profile_instrument) {
      const auto stats = passes::transform::RunInstrumentCallTrace(*ir_module, "poly");
      if (config.verbose) {
        std::cerr << "[pipeline/backend] call-trace instrumented "
                  << stats.functions_instrumented << "/" << stats.functions_visited
                  << " functions, +" << stats.enter_calls_inserted << " enter, +"
                  << stats.exit_calls_inserted << " exit\n";
      }
    }

    for (auto &fn : ir_module->Functions()) {
      ir::ConvertToSSA(*fn);
    }

    std::string verify_msg;
    ir::VerifyOptions verify_opts;
    verify_opts.strict = config.strict_mode;
    if (!ir::Verify(*ir_module, verify_opts, &verify_msg)) {
      diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                              frontends::ErrorCode::kLoweringUndefined,
                              "IR verification failed: " + verify_msg);
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    if (config.opt_level > 0) {
      passes::PassManager pm(static_cast<passes::PassManager::OptLevel>(config.opt_level));
      pm.Build();
      pm.RunOnModule(*ir_module, config.verbose);
    }

    // Translate a TargetArtifacts result from any backend (registered via
    // backends::BackendRegistry) into the pipeline's CompiledObject /
    // linker::{Symbol,Relocation} representation.  Replaces the previous
    // per-architecture if/else dispatch.
    auto absorb_artifacts = [&](const backends::TargetArtifacts &art,
                                const std::string &triple) {
      out.target_triple = triple;
      out.assembly_text = art.assembly_text;

      // For backends that produce a single self-contained binary (e.g. wasm)
      // the section list contains exactly one ".text" section that already
      // holds the final bytes.  Native targets produce one section per
      // emitted MC section.
      for (const auto &sec : art.sections) {
        CompiledObject obj;
        obj.name = sec.name;
        if (sec.name == ".text") {
          obj.code = sec.data;
        } else {
          obj.data = sec.data;
        }
        out.objects.push_back(std::move(obj));
      }

      for (const auto &sym : art.exported_symbols) {
        linker::Symbol ls;
        ls.name = sym.name;
        ls.section = sym.section;
        ls.offset = sym.value;
        ls.size = sym.size;
        ls.value = sym.value;
        ls.binding = sym.is_global ? linker::SymbolBinding::kGlobal
                                   : linker::SymbolBinding::kLocal;
        ls.type = linker::SymbolType::kFunction;
        ls.is_defined = sym.is_defined;

        for (auto &obj : out.objects) {
          if (obj.name == sym.section || (sym.section.empty() && obj.name == ".text")) {
            obj.symbols.push_back(ls);
            break;
          }
        }
      }

      for (const auto &rel : art.relocations) {
        linker::Relocation lr;
        lr.section = rel.section;
        lr.offset = rel.offset;
        lr.type = rel.type;
        lr.symbol = rel.symbol;
        lr.addend = rel.addend;
        const bool arm = config.target_arch == "arm64" || config.target_arch == "aarch64";
        lr.is_pc_relative = rel.type == 1 || (arm && rel.type == 2);
        lr.size = (rel.type == 1 || (arm && (rel.type == 2 || rel.type == 3))) ? 4 : 8;
        for (auto &obj : out.objects) {
          if (obj.name == rel.section || (rel.section.empty() && obj.name == ".text")) {
            obj.relocations.push_back(lr);
            break;
          }
        }
      }
    };

    // Resolve the backend through the global registry instead of the
    // architecture-string if/else chain that used to live here.  The triple
    // accepted on the command line may be any canonical triple or alias
    // declared by a registered backend.
    std::string lookup_diag;
    backends::ITargetBackend *backend =
        backends::BackendRegistry::Instance().FindOrDiagnose(config.target_arch, &lookup_diag);
    if (!backend) {
      diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                              frontends::ErrorCode::kLoweringUndefined, lookup_diag);
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    backends::TargetOptions backend_options;
    backend_options.emit = backends::EmitKind::kObject;
    backend_options.opt_level = config.opt_level;
    backend_options.target_os = RuntimeTargetOS(config);
    backend_options.force = config.force;
    backend_options.reg_alloc = config.reg_alloc;
    if (config.reg_alloc == backends::RegAllocStrategy::kStack &&
        config.target_arch != "arm64" && config.target_arch != "aarch64") {
      diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
          frontends::ErrorCode::kUnsupportedLowering, "stack allocation baseline requires ARM64");
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    backends::CompileResult bres = backend->Compile(*ir_module, backend_options);

    // Surface every backend diagnostic through the driver's diagnostics sink
    // so that --strict / --force semantics are honoured uniformly.
    for (const auto &d : bres.diagnostics) {
      if (d.severity == backends::BackendDiagnostic::Severity::kError) {
        diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                frontends::ErrorCode::kLoweringUndefined, d.message);
      } else {
        // Treat info-level entries as warnings so they remain visible without
        // failing the build; the Diagnostics interface has no separate info
        // channel.
        diagnostics.ReportWarning(core::SourceLoc{"<backend>", 1, 1},
                                  frontends::ErrorCode::kLoweringUndefined, d.message);
      }
    }

    if (!bres.ok) {
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    if (bres.artifacts.sections.empty() && bres.artifacts.object_bytes.empty()) {
      diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                              frontends::ErrorCode::kLoweringUndefined,
                              backend->TargetTriple() + " backend produced no sections");
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    absorb_artifacts(bres.artifacts, backend->TargetTriple());
    if (UsesNativeFileRuntime(*ir_module)) {
      std::string runtime_error;
      auto blob = runtime::BuildNativeFileRuntime(config.target_arch, RuntimeTargetOS(config),
                                                  &runtime_error);
      if (blob.text.empty()) {
        diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                frontends::ErrorCode::kLoweringUndefined, runtime_error);
        AppendDiagnostics(diagnostics, out.backend_diagnostics);
        out.success = false;
        return out;
      }

      CompiledObject runtime_object;
      runtime_object.name = ".text";
      runtime_object.code = std::move(blob.text);
      for (const auto &exported : blob.symbols) {
        linker::Symbol symbol;
        symbol.name = exported.name;
        symbol.section = ".text";
        symbol.offset = exported.offset;
        symbol.value = exported.offset;
        symbol.size = exported.size;
        symbol.binding = linker::SymbolBinding::kGlobal;
        symbol.type = linker::SymbolType::kFunction;
        symbol.is_defined = true;
        runtime_object.symbols.push_back(std::move(symbol));
      }
      out.objects.push_back(std::move(runtime_object));
    }
    out.ir_ctx = std::move(ir_module);
    out.success = true;
    return out;
  }
};

class DefaultPackagingStage final : public PackagingStage {
public:
  PackagingOutput Run(const BackendOutput &input, const CompilationContext::Config &config,
                      frontends::Diagnostics &diagnostics) override {
    PackagingOutput out;

    if (!input.success || input.objects.empty()) {
      diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                              frontends::ErrorCode::kLoweringUndefined,
                              "packaging requires backend objects");
      AppendDiagnostics(diagnostics, out.packaging_diagnostics);
      out.success = false;
      return out;
    }

    // WASM is emitted as a final binary directly.
    if (config.target_arch == "wasm" || config.target_arch == "wasm32" ||
        config.target_arch == "wasm64") {
      out.format = OutputFormat::kWasm;
      out.binary_data = input.objects.front().code;
      out.output_path = config.output_file;
      std::ofstream ofs(out.output_path, std::ios::binary | std::ios::trunc);
      if (!ofs.is_open()) {
        diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                frontends::ErrorCode::kUnresolvedSymbol,
                                "failed to open output file: " + out.output_path);
        AppendDiagnostics(diagnostics, out.packaging_diagnostics);
        out.success = false;
        return out;
      }
      ofs.write(reinterpret_cast<const char *>(out.binary_data.data()),
                static_cast<std::streamsize>(out.binary_data.size()));
      out.file_size = out.binary_data.size();
      out.success = ofs.good();
      AppendDiagnostics(diagnostics, out.packaging_diagnostics);
      return out;
    }

    std::vector<InternalSection> sections;
    std::unordered_map<std::string, std::uint32_t> sec_index;
    // PE-7-D: per-CompiledObject absorption record so the symbol and
    // relocation passes below can shift offsets when two objects target the
    // same merged section (e.g. two `.text` translation units).
    std::vector<ObjAbsorbInfo> absorb_log;
    absorb_log.reserve(input.objects.size());
    for (const auto &obj : input.objects) {
      absorb_log.push_back(AbsorbObjectSections(obj, sections, sec_index));
    }

    std::vector<InternalSymbol> symbols;
    // Keep deterministic entry symbol.
    symbols.push_back({"_start", sec_index.count(".text") ? sec_index[".text"] : 0, 0,
                       sections.empty() ? 0 : sections[0].data.size(), true, true});

    std::unordered_map<std::string, std::uint32_t> sym_index;
    sym_index["_start"] = 0;

    for (std::size_t oi = 0; oi < input.objects.size(); ++oi) {
      const auto &obj = input.objects[oi];
      const auto &log = absorb_log[oi];
      for (const auto &sym : obj.symbols) {
        // PE-7-D: a symbol's section may be `.text`/`.rdata`/... — look up
        // the merged section by *name*, not by the owning object's slot,
        // and shift the offset by the object-local base.
        const std::string sym_section = sym.section.empty() ? std::string(".text") : sym.section;
        const auto sec_it = sec_index.find(sym_section);
        const std::uint64_t section_base =
            (sec_it != sec_index.end() && sec_it->second == log.section_index) ? log.base_offset
                                                                                : 0;
        auto existing = sym_index.find(sym.name);
        if (existing != sym_index.end()) {
          // If the existing entry is undefined but this one is
          // defined, upgrade it so the object correctly marks
          // the symbol as a local definition.
          auto &prev = symbols[existing->second];
          if (!prev.defined && sym.is_defined && sec_it != sec_index.end()) {
            prev.section_index = sec_it->second;
            prev.defined = true;
            prev.value = sym.offset + section_base;
            prev.size = sym.size;
          }
          continue;
        }
        InternalSymbol s;
        s.name = sym.name;
        if (sym.is_defined && sec_it != sec_index.end()) {
          s.section_index = sec_it->second;
          s.defined = true;
        } else {
          s.section_index = 0xFFFFFFFF;
          s.defined = false;
        }
        s.value = sym.offset + section_base;
        s.size = sym.size;
        s.global = sym.is_global();
        sym_index[s.name] = static_cast<std::uint32_t>(symbols.size());
        symbols.push_back(std::move(s));
      }
    }

    // The deterministic public entry must refer to the source module's real
    // entry function, not blindly to byte zero of the merged text section.
    // This matters as soon as a Poly module contains helper functions before
    // `main`, and it also keeps the generated executable honest when foreign
    // source objects are absorbed later by the linker.
    for (const char *candidate : {"__polyc_entry", "__ploy_main", "main", "entry"}) {
      const auto entry_it = sym_index.find(candidate);
      if (entry_it == sym_index.end())
        continue;
      const auto &entry = symbols[entry_it->second];
      if (!entry.defined)
        continue;
      auto &start = symbols[sym_index["_start"]];
      start.section_index = entry.section_index;
      start.value = entry.value;
      start.size = entry.size;
      break;
    }

    for (std::size_t oi = 0; oi < input.objects.size(); ++oi) {
      const auto &obj = input.objects[oi];
      const auto &log = absorb_log[oi];
      for (auto rel : obj.relocations) {
        // PE-7-D: a relocation belongs to whatever section the backend says
        // it does (typically `.text`). Resolve via section name, then shift
        // its byte offset by this object's base inside the merged section.
        const std::string rel_section = rel.section.empty() ? std::string(".text") : rel.section;
        const auto si_it = sec_index.find(rel_section);
        if (si_it == sec_index.end())
          continue;
        const std::uint64_t section_base =
            (si_it->second == log.section_index) ? log.base_offset : 0;
        if (sym_index.count(rel.symbol) == 0) {
          InternalSymbol ext;
          ext.name = rel.symbol;
          ext.defined = false;
          ext.global = true;
          ext.section_index = 0xFFFFFFFF;
          sym_index[ext.name] = static_cast<std::uint32_t>(symbols.size());
          symbols.push_back(std::move(ext));
        }
        rel.symbol_index = static_cast<int>(sym_index[rel.symbol]);
        rel.offset += section_base;
        sections[si_it->second].relocs.push_back(std::move(rel));
      }
    }

    // PE-7-D: stable-sort the merged sections into the canonical loader
    // order (`.text → .rdata → .data → .bss → others`) and rewrite both
    // `sec_index` and every `InternalSymbol::section_index` so consumers
    // that key off raw indices stay correct after the reordering. Stable
    // sort preserves insertion order inside each priority bucket, which is
    // what backends that emit several custom sections rely on.
    std::vector<std::uint32_t> permutation(sections.size());
    for (std::size_t i = 0; i < permutation.size(); ++i)
      permutation[i] = static_cast<std::uint32_t>(i);
    std::stable_sort(permutation.begin(), permutation.end(),
                     [&](std::uint32_t a, std::uint32_t b) {
                       return SectionPriority(sections[a].name) <
                              SectionPriority(sections[b].name);
                     });
    bool needs_reorder = false;
    for (std::size_t i = 0; i < permutation.size(); ++i) {
      if (permutation[i] != i) {
        needs_reorder = true;
        break;
      }
    }
    if (needs_reorder) {
      std::vector<InternalSection> sorted_sections;
      sorted_sections.reserve(sections.size());
      std::vector<std::uint32_t> old_to_new(sections.size(), 0xFFFFFFFFu);
      for (std::size_t i = 0; i < permutation.size(); ++i) {
        old_to_new[permutation[i]] = static_cast<std::uint32_t>(i);
        sorted_sections.push_back(std::move(sections[permutation[i]]));
      }
      sections = std::move(sorted_sections);
      for (auto &kv : sec_index)
        kv.second = old_to_new[kv.second];
      for (auto &sym : symbols) {
        if (sym.section_index != 0xFFFFFFFFu)
          sym.section_index = old_to_new[sym.section_index];
      }
    }

    out.binary_data = BuildPobjBinary(sections, symbols);

    // Determine the effective object format.  If the user requested a
    // native format (elf/macho/coff) we emit that instead of POBJ so
    // that the platform linker can consume the file directly.  The
    // default ("pobj") is replaced with the host-native format.
    std::string effective_fmt = config.object_format;
    if (effective_fmt.empty() || effective_fmt == "pobj") {
      effective_fmt = HostObjectFormat();
    }

    // Build native object binary when a non-POBJ format is selected.
    std::vector<std::uint8_t> native_obj;
    if (effective_fmt != "pobj") {
      native_obj = BuildNativeObjectBinary(effective_fmt, config.target_arch, sections, symbols);
    }
    const auto &obj_data = native_obj.empty() ? out.binary_data : native_obj;

    out.format = OutputFormat::kStaticLib;

    std::string out_path = config.output_file;
    if (out_path.empty())
      out_path = "a.out";

    const std::string ext = ObjectExtension(effective_fmt);
    std::string obj_out = config.emit_obj_path;
    if (obj_out.empty()) {
      if (config.mode == "link") {
        obj_out = out_path + ext;
      } else {
        obj_out = (out_path == "a.out") ? (out_path + ext) : out_path;
      }
    }
    out.output_path = obj_out;

    std::ofstream ofs(obj_out, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                              frontends::ErrorCode::kUnresolvedSymbol,
                              "failed to create object output: " + obj_out);
      AppendDiagnostics(diagnostics, out.packaging_diagnostics);
      out.success = false;
      return out;
    }
    ofs.write(reinterpret_cast<const char *>(obj_data.data()),
              static_cast<std::streamsize>(obj_data.size()));
    ofs.close();

    if (!config.emit_asm_path.empty()) {
      std::ofstream asm_ofs(config.emit_asm_path, std::ios::binary | std::ios::trunc);
      if (asm_ofs.is_open()) {
        asm_ofs << input.assembly_text;
      }
    }

    if (!config.emit_ir_path.empty()) {
      // IR text is generated in backend stage only through internal context;
      // staged pipeline persists assembly/object at this stage.
      std::ofstream ir_ofs(config.emit_ir_path, std::ios::binary | std::ios::trunc);
      if (ir_ofs.is_open()) {
        if (input.ir_ctx) ir::PrintModule(*input.ir_ctx, ir_ofs);
      }
    }

    if (config.mode == "link") {
      LinkerChoice choice = SelectAvailableLinker(effective_fmt, config.polyld_path);
      if (choice.command_template.empty()) {
        // No linker available at all; keep the object and fail the requested
        // link operation with a structured diagnostic.
        diagnostics.ReportError(
            core::SourceLoc{"<packaging>", 1, 1}, frontends::ErrorCode::kUnresolvedSymbol,
            "no linker available for format '" + effective_fmt +
                "' (tried platform tools and bundled polyld); object kept at: " + obj_out);
        AppendDiagnostics(diagnostics, out.packaging_diagnostics);
        out.success = false;
        return out;
      } else {
        std::string stem = "output";
        if (!config.source_file.empty()) {
          stem = fs::path(config.source_file).stem().string();
        } else if (!out_path.empty()) {
          stem = fs::path(out_path).stem().string();
        }
        std::vector<std::string> link_inputs{obj_out};
        auto foreign_inputs = BuildForeignInputs(config, effective_fmt, stem, diagnostics);
        if (diagnostics.HasErrors()) {
          AppendDiagnostics(diagnostics, out.packaging_diagnostics);
          out.success = false;
          return out;
        }
        link_inputs.insert(link_inputs.end(), foreign_inputs.begin(), foreign_inputs.end());
        const std::string input_args = JoinCommandArgs(link_inputs);

        std::string cmd;
        if (link_inputs.size() == 1) {
          cmd = ExpandLinkCommand(choice, obj_out, out_path, config.ploy_desc_file, config.aux_dir);
        } else {
          cmd = choice.command_template;
          auto replace_all = [&](const std::string &needle, const std::string &value) {
            std::string::size_type pos = 0;
            while ((pos = cmd.find(needle, pos)) != std::string::npos) {
              cmd.replace(pos, needle.size(), value);
              pos += value.size();
            }
          };
          replace_all("{OBJ}", input_args);
          replace_all("{OUT}", ShellQuoteArg(out_path));
          if (choice.display_name.rfind("polyld", 0) == 0) {
            if (!config.ploy_desc_file.empty())
              cmd += " --poly-desc " + ShellQuoteArg(config.ploy_desc_file);
            if (!config.aux_dir.empty())
              cmd += " --aux-dir " + ShellQuoteArg(config.aux_dir);
          }
        }
        // BIN-7: when the chosen linker is the bundled polyld, forward
        // the resolved target-triple / container / subsystem / entry
        // descriptors so the linker reaches the same conclusion as the
        // driver instead of re-deriving them from the host macros.
        if (choice.display_name.rfind("polyld", 0) == 0) {
          cmd += " --target=" + config.target_triple.str();
          cmd += " --container=";
          switch (config.container) {
            case ::polyglot::common::BinaryContainer::kAuto:  cmd += "auto";  break;
            case ::polyglot::common::BinaryContainer::kELF:   cmd += "elf";   break;
            case ::polyglot::common::BinaryContainer::kPE:    cmd += "pe";    break;
            case ::polyglot::common::BinaryContainer::kMachO: cmd += "macho"; break;
            case ::polyglot::common::BinaryContainer::kWasm:  cmd += "wasm";  break;
          }
          if (!config.subsystem.empty()) {
            cmd += " --subsystem=" + config.subsystem;
          }
          if (!config.entry_symbol.empty()) {
            cmd += " --entry __polyc_entry";
          }
          if (link_inputs.size() > 1) {
            cmd += " --allow-multiple-definition";
          }
        }
        if (config.verbose) {
          std::cerr << "[polyc] Invoking " << choice.display_name << " -> " << out_path << "\n";
        }
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
          diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                  frontends::ErrorCode::kUnresolvedSymbol,
                                  "linker '" + choice.display_name +
                                      "' returned non-zero (object kept at " + obj_out +
                                      "): " + cmd);
          AppendDiagnostics(diagnostics, out.packaging_diagnostics);
          out.success = false;
          return out;
        }
        std::error_code ec;
        if (!std::filesystem::exists(out_path, ec)) {
          diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                  frontends::ErrorCode::kUnresolvedSymbol,
                                  "linker '" + choice.display_name +
                                      "' completed but did not produce output: " + out_path);
          AppendDiagnostics(diagnostics, out.packaging_diagnostics);
          out.success = false;
          return out;
        }
        out.output_path = out_path;
      }
    }

    std::error_code size_ec;
    if (std::filesystem::exists(out.output_path, size_ec)) {
      const auto disk_size = std::filesystem::file_size(out.output_path, size_ec);
      out.file_size = size_ec ? out.binary_data.size() : static_cast<size_t>(disk_size);
    } else {
      out.file_size = out.binary_data.size();
    }
    out.success = true;
    AppendDiagnostics(diagnostics, out.packaging_diagnostics);
    return out;
  }
};

} // namespace

// ---------------------------------------------------------------------------
// BIN-7: bidirectional sync between the canonical target triple and the
// legacy `target_arch` / `target_os` strings on `Config`.  These exist
// so old callers that still poke the strings stay valid while the new
// pipeline + downstream tools converge on `common::TargetTriple`.
// ---------------------------------------------------------------------------
void CompilationContext::Config::SetTargetTriple(
    const ::polyglot::common::TargetTriple &t) {
  target_triple = t;
  using ::polyglot::common::Arch;
  using ::polyglot::common::OS;
  switch (t.arch) {
    case Arch::kX86_64:  target_arch = "x86_64"; break;
    case Arch::kAArch64: target_arch = "arm64";  break;
    case Arch::kX86:     target_arch = "x86";    break;
    case Arch::kArm:     target_arch = "arm";    break;
    case Arch::kRiscv32: target_arch = "riscv32";break;
    case Arch::kRiscv64: target_arch = "riscv64";break;
    case Arch::kWasm32:  target_arch = "wasm";   break;
    case Arch::kWasm64:  target_arch = "wasm64"; break;
    default: break;
  }
  switch (t.os) {
    case OS::kLinux:   target_os = "linux"; break;
    case OS::kDarwin:  target_os = "macos"; break;
    case OS::kWindows: target_os = "windows"; break;
    case OS::kFreeBSD: target_os = "freebsd"; break;
    case OS::kWasi:    target_os = "wasi"; break;
    case OS::kNone:    target_os = "none"; break;
    case OS::kUnknown: /* leave as-is */ break;
  }
}

void CompilationContext::Config::SetTargetOs(const std::string &os) {
  target_os = os;
  // Fold the legacy short OS name into a canonical triple via the
  // shared parser so we never duplicate the OS-name table.  Pick a
  // sensible default arch when target_arch is empty.
  std::string arch_part = target_arch.empty() ? std::string{"x86_64"} : target_arch;
  if (arch_part == "arm64") arch_part = "aarch64";
  std::string spec;
  if (os == "windows")     spec = arch_part + "-pc-windows-msvc";
  else if (os == "macos" || os == "darwin")
                           spec = arch_part + "-apple-darwin";
  else if (os == "linux")  spec = arch_part + "-unknown-linux-gnu";
  else if (os == "wasi" || os == "wasm")
                           spec = "wasm32-wasi";
  if (!spec.empty()) {
    auto r = ::polyglot::common::ParseTargetTriple(spec);
    if (r.ok()) target_triple = *r.triple;
  }
}

CompilationPipeline::CompilationPipeline(CompilationContext::Config config) {
  config.source_language = CanonicalSourceLanguage(std::move(config.source_language));
  DiscoverProjectIncludePaths(config);
  context_.config = std::move(config);

  // ---- BIN-7: keep target_triple / target_os / target_arch in sync.
  // The driver may have populated any subset of those.  Resolve to a
  // single canonical triple via the dedicated setters defined just
  // below, then derive the legacy strings from that.
  using ::polyglot::common::Arch;
  using ::polyglot::common::HostTriple;
  using ::polyglot::common::OS;
  if (context_.config.target_triple.arch != Arch::kUnknown ||
      context_.config.target_triple.os   != OS::kUnknown) {
    // Caller already provided a triple — fold it into legacy fields.
    context_.config.SetTargetTriple(context_.config.target_triple);
  } else if (!context_.config.target_os.empty()) {
    context_.config.SetTargetOs(context_.config.target_os);
  } else {
    auto triple = HostTriple();
    const auto &arch = context_.config.target_arch;
    if (arch == "arm64" || arch == "aarch64") triple.arch = Arch::kAArch64;
    else if (arch == "x86_64") triple.arch = Arch::kX86_64;
    else if (arch == "wasm" || arch == "wasm32") { triple.arch = Arch::kWasm32; triple.os = OS::kWasi; }
    else if (arch == "wasm64") { triple.arch = Arch::kWasm64; triple.os = OS::kWasi; }
    else if (!arch.empty()) {
      // Keep an unknown explicit architecture so backend lookup diagnoses it.
      context_.config.target_triple = triple;
    }
    if (arch.empty() || arch == "arm64" || arch == "aarch64" || arch == "x86_64" ||
        arch == "wasm" || arch == "wasm32" || arch == "wasm64")
      context_.config.SetTargetTriple(triple);
  }

  context_.package_cache = std::make_shared<ploy::PackageDiscoveryCache>();
  frontend_stage_ = CreateFrontendStage();
  ::polyglot::tools::ForeignExtractionOptions foreign_options;
  if (!context_.config.source_file.empty()) {
    foreign_options.base_directory =
        fs::path(context_.config.source_file).parent_path().string();
  }
  foreign_options.poly_source_file = context_.config.source_file;
  foreign_options.bundle_directory = context_.config.aux_dir;
  foreign_options.require_local_source_packages = !context_.config.package_index;
  foreign_options.include_paths = context_.config.include_paths;
  foreign_options.verbose = context_.config.verbose;
  foreign_options.frontend_options = context_.config.frontend_options;
  foreign_options.frontend_options.strict = context_.config.strict_mode;
  foreign_options.frontend_options.force = context_.config.force;
  foreign_options.frontend_options.include_paths = context_.config.include_paths;
  foreign_options.frontend_options.system_include_paths = context_.config.system_include_paths;
  foreign_options.frontend_options.defines = context_.config.defines;
  foreign_options.frontend_options.undefines = context_.config.undefines;
  foreign_options.frontend_options.python_stub_paths = context_.config.python_stub_paths;
  foreign_options.frontend_options.classpath = context_.config.classpath;
  foreign_options.frontend_options.dotnet_references = context_.config.dotnet_references;
  foreign_options.frontend_options.rust_crate_dir = context_.config.rust_crate_dir;
  foreign_options.frontend_options.rust_externs = context_.config.rust_externs;
  semantic_stage_ = std::make_unique<DefaultSemanticStage>(context_.config.strict_mode,
                                                           std::move(foreign_options));
  marshal_plan_stage_ = CreateMarshalPlanStage();
  bridge_generation_stage_ = CreateBridgeGenerationStage();
  backend_stage_ = CreateBackendStage(context_.config.target_arch);
  packaging_stage_ = CreatePackagingStage();
}

bool CompilationPipeline::RunAll() {
  return RunFrontend() && RunSemantic() && RunMarshalPlan() && RunBridgeGeneration() &&
         RunBackend() && RunPackaging();
}

bool CompilationPipeline::RunFrontend() {
  auto start = high_resolution_clock::now();
  context_.frontend_output = frontend_stage_->Run(context_.config, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"frontend", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.frontend_output->success;
}

bool CompilationPipeline::RunSemantic() {
  if (!context_.frontend_output.has_value())
    return false;

  if (context_.config.package_index && context_.frontend_output->ast) {
    std::unordered_set<std::string> seen_languages;
    std::vector<std::string> languages;
    std::vector<ploy::VenvConfig> venvs;

    for (const auto &decl : context_.frontend_output->ast->declarations) {
      auto import = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
      if (!import || import->language.empty() || import->package_name.empty())
        continue;
      if (seen_languages.insert(import->language).second) {
        languages.push_back(import->language);
      }
    }

    if (!languages.empty()) {
      ploy::PackageIndexerOptions idx_opts;
      idx_opts.command_timeout = milliseconds{context_.config.package_index_timeout_ms};
      idx_opts.verbose = context_.config.verbose;

      auto runner = std::make_shared<ploy::DefaultCommandRunner>(idx_opts.command_timeout);
      ploy::PackageIndexer indexer(context_.package_cache, runner, idx_opts);
      // Forward CLI-supplied per-language project roots through the
      // VenvConfig channel so cargo metadata / mvn / gradle pick the
      // right working directory.
      if (!context_.config.rust_crate_dir.empty()) {
        ploy::VenvConfig vc;
        vc.language = "rust";
        vc.venv_path = context_.config.rust_crate_dir;
        vc.manager = ploy::VenvConfigDecl::ManagerKind::kVenv;
        venvs.push_back(std::move(vc));
      }
      indexer.BuildIndex(languages, venvs);
    }
  }

  auto start = high_resolution_clock::now();
  context_.semantic_db = semantic_stage_->Run(*context_.frontend_output, *context_.diagnostics,
                                              context_.package_cache);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"semantic-db", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.semantic_db->success;
}

bool CompilationPipeline::RunMarshalPlan() {
  if (!context_.semantic_db.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.marshal_plan = marshal_plan_stage_->Run(*context_.semantic_db, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"marshal-plan", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.marshal_plan->success;
}

bool CompilationPipeline::RunBridgeGeneration() {
  if (!context_.marshal_plan.has_value() || !context_.semantic_db.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.bridge_output = bridge_generation_stage_->Run(
      *context_.marshal_plan, *context_.semantic_db, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"bridge-generation", std::chrono::duration<double, std::milli>(end - start).count()});
  if (!context_.bridge_output->success)
    return false;

  // ── Serialize cross-language descriptors to a PAUX text file ──────────
  // The file uses the same text format that PolyglotLinker::LoadDescriptorFile()
  // understands (LINK / CALL / SYMBOL lines), so polyld can ingest it via
  // --poly-desc without any additional parsing logic.
  if (!context_.config.aux_dir.empty() && context_.semantic_db.has_value()) {
    namespace fs = std::filesystem;
    std::string stem;
    if (!context_.config.source_file.empty()) {
      stem = fs::path(context_.config.source_file).stem().string();
    } else {
      stem = "output";
    }

    fs::path desc_path = fs::path(context_.config.aux_dir) / (stem + "_link_descriptors.paux");

    std::ofstream ofs(desc_path);
    if (ofs.is_open()) {
      ofs << "# PolyglotCompiler cross-language descriptor file\n";
      ofs << "# Generated by polyc bridge stage — do not edit manually\n";

      // Emit LINK entries (from sema) with MAP_TYPE sub-entries
      for (const auto &entry : context_.semantic_db->link_entries) {
        ofs << "LINK " << entry.target_language << " " << entry.source_language << " "
            << entry.target_symbol << " " << entry.source_symbol << "\n";
        for (const auto &m : entry.param_mappings) {
          ofs << "MAP_TYPE";
          if (!m.source_language.empty())
            ofs << " " << m.source_language;
          else
            ofs << " " << entry.target_language;
          ofs << "::" << m.source_type;
          if (!m.target_language.empty())
            ofs << " " << m.target_language;
          else
            ofs << " " << entry.source_language;
          ofs << "::" << m.target_type << "\n";
        }
      }

      // Every local source import is a build dependency even when only some
      // of its functions are called.  The packaging stage consumes these
      // rows to auto-compile the module with the matching built-in frontend;
      // polyld treats them as provenance metadata.
      if (context_.semantic_db->validated_ast) {
        for (const auto &decl : context_.semantic_db->validated_ast->declarations) {
          auto import = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
          if (import && !import->language.empty() && !import->module_path.empty() &&
              import->package_name.empty()) {
            ofs << "IMPORT " << import->language << " " << import->module_path << "\n";
          }
        }
      }

      // Emit CALL descriptors (from lowering, carried through marshal plan)
      for (const auto &cp : context_.marshal_plan->call_plans) {
        if (cp.is_native_import)
          continue;
        // Mirror the mangling rule from `MangleStubName` in poly lowering:
        // include a `_v<sanitized_version>_` segment when a version is
        // pinned so polyld can resolve the right versioned bridge.
        std::string stub_name = "__ploy_bridge_" + AbiLanguageToken(cp.target_language) + "_" +
                                AbiLanguageToken(cp.source_language) + "_";
        if (!cp.lang_version.empty()) {
          stub_name += "v";
          for (char c : cp.lang_version) {
            stub_name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
          }
          stub_name.push_back('_');
        }
        stub_name += cp.target_function;
        ofs << "CALL " << stub_name << " " << cp.source_language << " " << cp.target_language << " "
            << cp.source_function << " " << cp.target_function << "\n";
        if (!cp.lang_version.empty()) {
          ofs << "VERSION " << cp.source_language << " " << cp.lang_version << "\n";
        }
      }

      // Emit SYMBOL entries derived from sema signatures so polyld can
      // reconstruct CrossLangSymbol without re-running sema.
      std::unordered_set<std::string> seen;
      auto emit_sym = [&](const std::string &name, const std::string &lang) {
        std::string key = lang + "::" + name;
        if (!seen.insert(key).second)
          return;
        ofs << "SYMBOL " << name << " " << lang << " " << name << "\n";
      };
      for (const auto &entry : context_.semantic_db->link_entries) {
        emit_sym(entry.target_symbol, entry.target_language);
        emit_sym(entry.source_symbol, entry.source_language);
      }
      for (const auto &cp : context_.marshal_plan->call_plans) {
        if (cp.is_native_import) {
          emit_sym(cp.source_function, cp.source_language);
        } else {
          emit_sym(cp.target_function, cp.target_language);
          emit_sym(cp.source_function, cp.source_language);
        }
      }

      ofs.close();
      context_.config.ploy_desc_file = desc_path.string();
      if (context_.config.verbose) {
        std::cerr << "[pipeline] link descriptors -> " << desc_path.string() << "\n";
      }
    }
  }

  return true;
}

bool CompilationPipeline::RunBackend() {
  if (!context_.semantic_db.has_value() || !context_.bridge_output.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.backend_output = backend_stage_->Run(*context_.semantic_db, *context_.bridge_output,
                                                context_.config, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"backend", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.backend_output->success;
}

bool CompilationPipeline::RunPackaging() {
  if (!context_.backend_output.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.packaging_output =
      packaging_stage_->Run(*context_.backend_output, context_.config, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"packaging", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.packaging_output->success;
}

PackagingOutput *CompilationPipeline::GetFinalOutput() {
  if (!context_.packaging_output.has_value())
    return nullptr;
  return &(*context_.packaging_output);
}

const FrontendOutput *CompilationPipeline::GetFrontendOutput() const {
  return context_.frontend_output ? &(*context_.frontend_output) : nullptr;
}

const SemanticDatabase *CompilationPipeline::GetSemanticDb() const {
  return context_.semantic_db ? &(*context_.semantic_db) : nullptr;
}

const MarshalPlan *CompilationPipeline::GetMarshalPlan() const {
  return context_.marshal_plan ? &(*context_.marshal_plan) : nullptr;
}

const BridgeGenerationOutput *CompilationPipeline::GetBridgeOutput() const {
  return context_.bridge_output ? &(*context_.bridge_output) : nullptr;
}

const BackendOutput *CompilationPipeline::GetBackendOutput() const {
  return context_.backend_output ? &(*context_.backend_output) : nullptr;
}

std::unique_ptr<FrontendStage> CreateFrontendStage() {
  return std::make_unique<DefaultFrontendStage>();
}

std::unique_ptr<SemanticStage> CreateSemanticStage() {
  return std::make_unique<DefaultSemanticStage>();
}

std::unique_ptr<MarshalPlanStage> CreateMarshalPlanStage() {
  return std::make_unique<DefaultMarshalPlanStage>();
}

std::unique_ptr<BridgeGenerationStage> CreateBridgeGenerationStage() {
  return std::make_unique<DefaultBridgeGenerationStage>();
}

std::unique_ptr<BackendStage> CreateBackendStage(const std::string &target_arch) {
  (void)target_arch;
  return std::make_unique<DefaultBackendStage>();
}

std::unique_ptr<PackagingStage> CreatePackagingStage() {
  return std::make_unique<DefaultPackagingStage>();
}

} // namespace polyglot::compilation
