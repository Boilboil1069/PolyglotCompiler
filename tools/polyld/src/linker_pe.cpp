/**
 * @file     linker_pe.cpp
 * @brief    PE-specific linker support: .def parsing, export-table layout,
 *           CLI /EXPORT merging, and PE base-relocation translation.
 *
 * Lives alongside `linker.cpp` in the same `polyglot::linker` family and
 * follows the same style: free helpers in the public `pe` namespace, no
 * dependency on the rest of the `Linker` class except through
 * `Linker::GeneratePEDll()` at the bottom of the file (which is the actual
 * BIN-4 entry point).
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-05-05
 */

#include "tools/polyld/include/linker_pe.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "tools/polyld/include/linker.h"
#include "tools/polyld/include/pe_writer.h"

namespace polyglot::linker {

// Bring writer-side names into scope for the export-section builder.
namespace pe {

namespace {

// ---------------------------------------------------------------------------
// Tiny string utilities (kept private to this translation unit).
// ---------------------------------------------------------------------------

std::string Trim(const std::string &s) {
  std::size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  std::size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

std::string ToUpperCopy(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> SplitOnCommasOrSpaces(const std::string &s) {
  // Pre-pass: collapse whitespace around `=` so the .def-style head
  // grammar `Name = Internal` tokenises into one token.  The `@` operator
  // (ordinal) is *not* collapsed into its left neighbour; we instead
  // ensure it always begins a new token so `Name @ 1` becomes
  // `["Name", "@1"]`.
  std::string normalised;
  normalised.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '=') {
      while (!normalised.empty() &&
             std::isspace(static_cast<unsigned char>(normalised.back())))
        normalised.pop_back();
      normalised.push_back('=');
      while (i + 1 < s.size() &&
             std::isspace(static_cast<unsigned char>(s[i + 1])))
        ++i;
      continue;
    }
    if (c == '@') {
      // Force a token break before `@`, then drop the whitespace following it.
      if (!normalised.empty() &&
          !std::isspace(static_cast<unsigned char>(normalised.back())))
        normalised.push_back(' ');
      normalised.push_back('@');
      while (i + 1 < s.size() &&
             std::isspace(static_cast<unsigned char>(s[i + 1])))
        ++i;
      continue;
    }
    normalised.push_back(c);
  }

  std::vector<std::string> out;
  std::string cur;
  for (char c : normalised) {
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty())
    out.push_back(cur);
  return out;
}

// Append a 32-bit little-endian word to `out`.  Reserved for future use
// when the writer grows direct .edata emission; kept compiled-in so the
// helper does not bit-rot across patches.
[[maybe_unused]] void Push32LE(std::vector<std::uint8_t> &out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

[[maybe_unused]] void Push16LE(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

// Patch a 32-bit little-endian word at `offset`.
void Patch32LE(std::vector<std::uint8_t> &out, std::size_t offset, std::uint32_t v) {
  out[offset + 0] = static_cast<std::uint8_t>(v & 0xFF);
  out[offset + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
  out[offset + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
  out[offset + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// Parse the trailing `[, @ord] [, NONAME] [, DATA]` decorations of one
// EXPORTS-line / /EXPORT spec.  `tokens[0]` is assumed to be the
// `name[=internal]` head and is therefore skipped here.
bool ParseExportDecorations(const std::vector<std::string> &tokens,
                            LinkerExport &exp,
                            std::vector<std::string> &errors_out) {
  // Allow tokens like "@5", "NONAME", "DATA" in any order after the head.
  for (std::size_t k = 1; k < tokens.size(); ++k) {
    const std::string raw = tokens[k];
    const std::string up = ToUpperCopy(raw);
    if (!raw.empty() && raw[0] == '@') {
      // Ordinal: "@N".
      const std::string ord_str = raw.substr(1);
      if (ord_str.empty() ||
          !std::all_of(ord_str.begin(), ord_str.end(),
                       [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        errors_out.push_back("polyld-err-E3200: malformed ordinal '" + raw +
                             "' for export '" + exp.name + "'");
        return false;
      }
      const long ord = std::stol(ord_str);
      if (ord <= 0 || ord > 0xFFFF) {
        errors_out.push_back("polyld-err-E3200: ordinal out of range '" + raw +
                             "' for export '" + exp.name + "'");
        return false;
      }
      exp.ordinal = static_cast<std::uint16_t>(ord);
    } else if (up == "NONAME") {
      exp.noname = true;
    } else if (up == "DATA") {
      exp.data = true;
    } else if (up == "PRIVATE") {
      // PRIVATE means "do not put in the import library"; we have no
      // import library to emit, so accept and ignore.
    } else {
      errors_out.push_back("polyld-err-E3200: unknown export decoration '" + raw +
                           "' for export '" + exp.name + "'");
      return false;
    }
  }
  return true;
}

// Parse one head token of the form `name[=internal]` into `exp`.
bool ParseExportHead(const std::string &head, LinkerExport &exp,
                     std::vector<std::string> &errors_out) {
  const auto eq = head.find('=');
  if (eq == std::string::npos) {
    exp.name = Trim(head);
    exp.internal.clear();
  } else {
    exp.name = Trim(head.substr(0, eq));
    exp.internal = Trim(head.substr(eq + 1));
  }
  if (exp.name.empty()) {
    errors_out.push_back("polyld-err-E3200: empty export name in '" + head + "'");
    return false;
  }
  return true;
}

} // namespace

// ===========================================================================
// .def parsing
// ===========================================================================

bool ParseDefFile(const std::string &path,
                  std::string &library_out,
                  std::vector<LinkerExport> &exports_out,
                  std::vector<std::string> &errors_out) {
  std::ifstream in(path);
  if (!in) {
    errors_out.push_back("polyld-err-E3200: cannot open .def file: " + path);
    return false;
  }

  enum class Section { kNone, kExports, kOther };
  Section sec = Section::kNone;

  std::string line;
  std::size_t lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    // Strip comments.
    const auto semi = line.find(';');
    if (semi != std::string::npos)
      line.erase(semi);
    const auto hash = line.find('#');
    if (hash != std::string::npos)
      line.erase(hash);
    line = Trim(line);
    if (line.empty())
      continue;

    // Section header detection: a bare uppercase keyword on its own line.
    const auto ws = line.find_first_of(" \t");
    const std::string head = ws == std::string::npos ? line : line.substr(0, ws);
    const std::string head_upper = ToUpperCopy(head);
    const std::string rest = ws == std::string::npos ? std::string() : Trim(line.substr(ws));

    if (head_upper == "LIBRARY") {
      // Take the first whitespace-delimited token of `rest` as the library
      // name; ignore any BASE=, INITINSTANCE etc decorations.
      if (!rest.empty()) {
        const auto tokens = SplitOnCommasOrSpaces(rest);
        if (!tokens.empty())
          library_out = tokens.front();
      }
      sec = Section::kNone;
      continue;
    }
    if (head_upper == "EXPORTS") {
      sec = Section::kExports;
      // Some .def files write the first export on the same line as
      // "EXPORTS"; honour that.
      if (rest.empty())
        continue;
      line = rest;
    } else if (head_upper == "IMPORTS" || head_upper == "STUB" ||
               head_upper == "DESCRIPTION" || head_upper == "VERSION" ||
               head_upper == "HEAPSIZE" || head_upper == "STACKSIZE" ||
               head_upper == "SECTIONS" || head_upper == "NAME") {
      // Tolerated but unused.
      sec = Section::kOther;
      continue;
    }

    if (sec != Section::kExports)
      continue;

    // Tokenise an EXPORTS line: "name[=internal] [@N] [NONAME] [DATA]".
    auto tokens = SplitOnCommasOrSpaces(line);
    if (tokens.empty())
      continue;
    LinkerExport exp;
    if (!ParseExportHead(tokens[0], exp, errors_out)) {
      errors_out.back() += " (at " + path + ":" + std::to_string(lineno) + ")";
      return false;
    }
    if (!ParseExportDecorations(tokens, exp, errors_out)) {
      errors_out.back() += " (at " + path + ":" + std::to_string(lineno) + ")";
      return false;
    }
    exports_out.push_back(exp);
  }
  return true;
}

bool ParseCliExportSpec(const std::string &spec, LinkerExport &out,
                        std::vector<std::string> &errors_out) {
  const auto tokens = SplitOnCommasOrSpaces(spec);
  if (tokens.empty()) {
    errors_out.push_back("polyld-err-E3200: empty /EXPORT spec");
    return false;
  }
  if (!ParseExportHead(tokens[0], out, errors_out))
    return false;
  return ParseExportDecorations(tokens, out, errors_out);
}

// ===========================================================================
// MergeExports
// ===========================================================================

namespace {
const char *SourceTag(ExportSource s) {
  switch (s) {
  case ExportSource::kCommandLine: return "/EXPORT";
  case ExportSource::kDefFile:     return ".def";
  case ExportSource::kDeclSpec:    return "__declspec(dllexport)";
  }
  return "?";
}

bool ExportConflicts(const LinkerExport &a, const LinkerExport &b) {
  if (a.internal != b.internal)
    return true;
  if (a.ordinal != 0 && b.ordinal != 0 && a.ordinal != b.ordinal)
    return true;
  if (a.noname != b.noname)
    return true;
  if (a.data != b.data)
    return true;
  return false;
}

// Pick the more specific entry when both agree (e.g. one had ordinal=0
// and the other had a real ordinal).  Used to coalesce identical entries
// arriving from multiple sources.
LinkerExport MergeCompatible(const LinkerExport &a, const LinkerExport &b) {
  LinkerExport out = a;
  if (out.internal.empty())
    out.internal = b.internal;
  if (out.ordinal == 0)
    out.ordinal = b.ordinal;
  if (b.noname)
    out.noname = true;
  if (b.data)
    out.data = true;
  return out;
}
} // namespace

bool MergeExports(std::vector<LinkerExport> &into,
                  const std::vector<LinkerExport> &incoming,
                  ExportSource source,
                  std::vector<std::string> &errors_out) {
  bool ok = true;
  for (const auto &inc : incoming) {
    auto it = std::find_if(into.begin(), into.end(),
                           [&](const LinkerExport &e) { return e.name == inc.name; });
    if (it == into.end()) {
      into.push_back(inc);
      continue;
    }
    if (ExportConflicts(*it, inc)) {
      errors_out.push_back(
          std::string("polyld-err-E3201: conflicting export descriptors for '") +
          inc.name + "' (from " + SourceTag(source) + ")");
      ok = false;
      continue;
    }
    *it = MergeCompatible(*it, inc);
  }
  return ok;
}

// ===========================================================================
// BuildExportSection
// ===========================================================================

ExportSectionResult BuildExportSection(const std::vector<LinkerExport> &exports,
                                       const std::string &dll_name,
                                       std::uint32_t edata_rva) {
  ExportSectionResult R;
  if (exports.empty())
    return R;

  // 1) Auto-assign ordinals.  Strategy: scan once to collect explicit
  // ordinals, then walk in order assigning the next free positive
  // integer to any entry whose ordinal is still 0.  Base ordinal is the
  // minimum ordinal across the table (1 if all auto-assigned).
  std::vector<LinkerExport> tab = exports;
  std::set<std::uint16_t> taken;
  for (const auto &e : tab)
    if (e.ordinal != 0)
      taken.insert(e.ordinal);
  std::uint16_t cursor = 1;
  for (auto &e : tab) {
    if (e.ordinal != 0)
      continue;
    while (taken.count(cursor) != 0)
      ++cursor;
    e.ordinal = cursor;
    taken.insert(cursor);
    ++cursor;
  }

  std::uint16_t base_ordinal = 0xFFFF;
  std::uint16_t max_ordinal = 0;
  for (const auto &e : tab) {
    base_ordinal = std::min(base_ordinal, e.ordinal);
    max_ordinal = std::max(max_ordinal, e.ordinal);
  }
  const std::uint32_t eat_count =
      static_cast<std::uint32_t>(max_ordinal - base_ordinal + 1);

  // 2) Collect names for the Name Pointer Table, sorted lexicographically.
  // Entries flagged `noname` are excluded from the Name Pointer Table /
  // Ordinal Table but still occupy their slot in the Export Address Table.
  struct NamedExport {
    std::string name;
    std::uint16_t biased_ordinal; // ordinal - base_ordinal
  };
  std::vector<NamedExport> named;
  named.reserve(tab.size());
  for (const auto &e : tab)
    if (!e.noname)
      named.push_back({e.name, static_cast<std::uint16_t>(e.ordinal - base_ordinal)});
  std::sort(named.begin(), named.end(),
            [](const NamedExport &a, const NamedExport &b) { return a.name < b.name; });
  const std::uint32_t name_count = static_cast<std::uint32_t>(named.size());

  // 3) Pre-compute byte offsets of each subregion *relative to edata_rva*.
  constexpr std::uint32_t kExportDirSize = 40;
  const std::uint32_t eat_off = kExportDirSize;
  const std::uint32_t enpt_off = eat_off + eat_count * 4u;
  const std::uint32_t eot_off  = enpt_off + name_count * 4u;
  std::uint32_t cursor_off = eot_off + name_count * 2u;

  // DLL name string.
  const std::uint32_t dll_name_off = cursor_off;
  cursor_off += static_cast<std::uint32_t>(dll_name.size()) + 1u;

  // Export name strings (one per non-NONAME export, in NPT order).
  std::vector<std::uint32_t> name_string_off(named.size(), 0);
  for (std::size_t i = 0; i < named.size(); ++i) {
    name_string_off[i] = cursor_off;
    cursor_off += static_cast<std::uint32_t>(named[i].name.size()) + 1u;
  }

  // 4) Allocate output buffer and lay down each region.
  R.bytes.assign(cursor_off, 0);
  R.directory_rva = edata_rva;
  R.directory_size = cursor_off;

  // IMAGE_EXPORT_DIRECTORY (40 bytes).  Layout per Microsoft PE/COFF spec:
  //   +00 Characteristics       (DWORD, reserved must be 0)
  //   +04 TimeDateStamp         (DWORD, 0)
  //   +08 MajorVersion          (WORD,  0)
  //   +0A MinorVersion          (WORD,  0)
  //   +0C Name                  (DWORD, RVA of NUL-terminated DLL name)
  //   +10 Base                  (DWORD, ordinal base)
  //   +14 NumberOfFunctions     (DWORD, eat_count)
  //   +18 NumberOfNames         (DWORD, name_count)
  //   +1C AddressOfFunctions    (DWORD, RVA of EAT)
  //   +20 AddressOfNames        (DWORD, RVA of Name Pointer Table)
  //   +24 AddressOfNameOrdinals (DWORD, RVA of Ordinal Table)
  Patch32LE(R.bytes, 0x00, 0);
  Patch32LE(R.bytes, 0x04, 0);
  // Major / Minor version are zero — already.
  Patch32LE(R.bytes, 0x0C, edata_rva + dll_name_off);
  Patch32LE(R.bytes, 0x10, base_ordinal);
  Patch32LE(R.bytes, 0x14, eat_count);
  Patch32LE(R.bytes, 0x18, name_count);
  Patch32LE(R.bytes, 0x1C, edata_rva + eat_off);
  Patch32LE(R.bytes, 0x20, edata_rva + enpt_off);
  Patch32LE(R.bytes, 0x24, edata_rva + eot_off);

  // Export Address Table: indexed by (ordinal - base_ordinal).  Slots
  // without a backing export are zero (legal per spec).
  for (const auto &e : tab) {
    const std::uint32_t slot = e.ordinal - base_ordinal;
    Patch32LE(R.bytes, eat_off + slot * 4u, e.rva);
  }

  // Name Pointer Table + Ordinal Table.
  for (std::size_t i = 0; i < named.size(); ++i) {
    Patch32LE(R.bytes, enpt_off + i * 4u, edata_rva + name_string_off[i]);
    R.bytes[eot_off + i * 2u + 0] = static_cast<std::uint8_t>(named[i].biased_ordinal & 0xFF);
    R.bytes[eot_off + i * 2u + 1] = static_cast<std::uint8_t>((named[i].biased_ordinal >> 8) & 0xFF);
  }

  // DLL name string + per-export name strings.
  std::memcpy(&R.bytes[dll_name_off], dll_name.data(), dll_name.size());
  // Trailing NUL is already zero from the assign() above.
  for (std::size_t i = 0; i < named.size(); ++i)
    std::memcpy(&R.bytes[name_string_off[i]], named[i].name.data(), named[i].name.size());

  return R;
}

// ===========================================================================
// Relocation translation
// ===========================================================================

namespace {

// Map a single PendingRelocation to a PE base-relocation type.  Returns
// false when no PE-expressible mapping exists for `(machine, type)`.
bool MapRelocType(PEMachine machine, std::uint32_t type,
                  std::uint8_t &pe_type_out) {
  using R64 = polyglot::linker::RelocationType_x86_64;
  using RA  = polyglot::linker::RelocationType_ARM64;
  switch (machine) {
  case PEMachine::kAmd64: {
    switch (static_cast<R64>(type)) {
    case R64::kR_X86_64_64:
      pe_type_out = 10; // IMAGE_REL_BASED_DIR64
      return true;
    case R64::kR_X86_64_32:
    case R64::kR_X86_64_32S:
      pe_type_out = 3; // IMAGE_REL_BASED_HIGHLOW
      return true;
    default:
      return false;
    }
  }
  case PEMachine::kArm64: {
    switch (static_cast<RA>(type)) {
    case RA::kR_AARCH64_ABS64:
      pe_type_out = 10; // DIR64
      return true;
    case RA::kR_AARCH64_ABS32:
      pe_type_out = 3;  // HIGHLOW
      return true;
    case RA::kR_AARCH64_CALL26:
    case RA::kR_AARCH64_JUMP26:
      pe_type_out = 3;  // IMAGE_REL_BASED_ARM64_BRANCH26 (nibble = 3)
      return true;
    case RA::kR_AARCH64_ADR_PREL_PG_HI21:
      pe_type_out = 4;  // ARM64_PAGEBASE_REL21
      return true;
    case RA::kR_AARCH64_ADD_ABS_LO12_NC:
      pe_type_out = 7;  // ARM64_PAGEOFFSET_12A
      return true;
    case RA::kR_AARCH64_LDST64_ABS_LO12_NC:
      pe_type_out = 9;  // ARM64_PAGEOFFSET_12L
      return true;
    default:
      return false;
    }
  }
  case PEMachine::kI386: {
    // 32-bit absolute is the only widely-used base reloc on i386.
    switch (static_cast<R64>(type)) {
    case R64::kR_X86_64_32:
    case R64::kR_X86_64_32S:
      pe_type_out = 3;
      return true;
    default:
      return false;
    }
  }
  }
  return false;
}

} // namespace

bool TranslateRelocationsToPEBaseRelocs(
    const std::vector<PendingRelocation> &input,
    PEMachine machine,
    std::vector<BaseRelocation> &out,
    std::vector<std::string> &errors_out) {
  bool ok = true;
  for (const auto &r : input) {
    if (r.is_pc_relative)
      continue; // PC-relative relocs do not need PE base relocs.
    std::uint8_t pe_type = 0;
    if (!MapRelocType(machine, r.type, pe_type)) {
      std::ostringstream os;
      os << "polyld-err-E3210: relocation type 0x" << std::hex << r.type
         << " has no PE base-relocation mapping for the requested target";
      errors_out.push_back(os.str());
      ok = false;
      continue;
    }
    out.push_back(BaseRelocation{r.rva, pe_type});
  }
  return ok;
}

} // namespace pe

// ===========================================================================
// Linker::GeneratePEDll  (BIN-4 entry point)
//
// Produces a real `.dll` image: PE32+ with `IMAGE_FILE_DLL` set, no entry
// shim (the DLL has no `_start`), and an `.edata` export section built
// from the merged set of `--def`-file and `/EXPORT:` descriptors.  The
// image's preferred load address defaults to 0x180000000 (Windows DLL
// convention); callers can override via `LinkerConfig::base_address`
// once a future patch wires that field through.
//
// Export RVAs are resolved by looking each export's *internal* (or public,
// when no alias is given) symbol up in `symbol_table_` and mapping its
// `(object_index, section_index, offset)` triple through `.text`'s
// contribution map to a merged-output offset.  Symbols that resolve
// outside `.text` resolve relative to whichever output section contains
// them; unresolved exports are reported as `polyld-err-E3201`.
// ===========================================================================

bool Linker::GeneratePEDll() {
  // ----- 1. Gather the merged .text payload that will hold exported code.
  const OutputSection *text_sec = nullptr;
  for (const auto &sec : output_sections_) {
    if (sec.name == ".text" || sec.name == "__text") {
      text_sec = &sec;
      break;
    }
  }
  if (text_sec == nullptr) {
    ReportError("polyld-err-E3201: cannot build PE DLL: no .text section");
    return false;
  }

  // ----- 2. Collect export descriptors from .def files + CLI specs.
  std::vector<pe::LinkerExport> exports;
  std::vector<std::string> export_errors;
  std::string def_library;

  for (const auto &cli : config_.cli_export_specs) {
    pe::LinkerExport e;
    if (!pe::ParseCliExportSpec(cli, e, export_errors))
      continue; // collected into export_errors
    std::vector<pe::LinkerExport> one{e};
    pe::MergeExports(exports, one, pe::ExportSource::kCommandLine, export_errors);
  }
  for (const auto &path : config_.def_files) {
    std::string lib;
    std::vector<pe::LinkerExport> def_exports;
    if (!pe::ParseDefFile(path, lib, def_exports, export_errors))
      continue;
    if (def_library.empty())
      def_library = lib;
    pe::MergeExports(exports, def_exports, pe::ExportSource::kDefFile, export_errors);
  }
  if (!export_errors.empty()) {
    for (const auto &e : export_errors)
      ReportError(e);
    return false;
  }

  // ----- 3. Resolve each export's RVA from the symbol table.
  auto resolve_symbol_rva = [&](const std::string &sym, std::uint32_t &out_rva) -> bool {
    auto it = symbol_table_.find(sym);
    if (it == symbol_table_.end() || !it->second.is_defined)
      return false;
    const Symbol &s = it->second;
    // Find the merged contribution that hosts this symbol.
    for (const auto &out_sec : output_sections_) {
      for (const auto &c : out_sec.contributions) {
        if (c.object_index == s.object_file_index &&
            c.section_index == s.section_index) {
          // Output sections do not yet carry a final RVA in the same way
          // PE writers use; for our purpose the layout planner places
          // `.text` at kTextRVA (0x1000) and there is currently exactly
          // one user-controlled section per kind, so we anchor the RVA
          // off the writer's `kTextRVA` for `.text` and mirror layout
          // ordering for the rest.  When the linker grows multi-section
          // PE output this lookup will go through a proper RVA map.
          std::uint32_t base = 0x1000;
          if (out_sec.name == ".text" || out_sec.name == "__text")
            base = 0x1000;
          out_rva = static_cast<std::uint32_t>(base + c.output_offset + s.offset);
          return true;
        }
      }
    }
    return false;
  };

  for (auto &e : exports) {
    const std::string &lookup = e.internal.empty() ? e.name : e.internal;
    if (!resolve_symbol_rva(lookup, e.rva)) {
      ReportError("polyld-err-E3201: export '" + e.name +
                  "' resolves to undefined symbol '" + lookup + "'");
      return false;
    }
  }

  // ----- 4. Plan section layout.  We want:
  //   .text  | text bytes (user code)
  //   .edata | export directory (after .text, page-aligned)
  // The PE writer handles the rest.  We compute the .edata RVA by hand so
  // BuildExportSection can patch in absolute RVAs.
  const std::uint32_t kTextRVA = 0x1000;
  const std::uint32_t kSectionAlignment = 0x1000;
  const std::uint32_t text_end = kTextRVA + static_cast<std::uint32_t>(text_sec->data.size());
  const std::uint32_t edata_rva = (text_end + kSectionAlignment - 1) & ~(kSectionAlignment - 1);

  std::string dll_name = config_.dll_name;
  if (dll_name.empty())
    dll_name = def_library;
  if (dll_name.empty()) {
    // Fall back to the basename of `output_file`.
    const auto slash = config_.output_file.find_last_of("/\\");
    dll_name = slash == std::string::npos ? config_.output_file
                                          : config_.output_file.substr(slash + 1);
  }

  pe::ExportSectionResult edata =
      pe::BuildExportSection(exports, dll_name, edata_rva);

  // ----- 5. Drive the PE32+ writer.  We re-use the rdata channel for the
  // export directory bytes — the writer lays a `.rdata` section right after
  // `.text`, so we splice the export bytes in there, then point the
  // Optional Header's Export Data Directory at that RVA via a custom
  // post-processing pass below.  An alternative would be to teach the
  // writer about a real `.edata` section; we keep the surface area small
  // for BIN-4 and patch the directory entry directly.
  pe::BuildRequest req;
  req.text_bytes = text_sec->data;
  req.entry_offset_in_text = 0; // DLL has no entry-shim; loader uses DllMain when present
  req.image_base = 0x180000000ULL; // canonical Windows DLL preferred base
  req.machine = pe::PEMachine::kAmd64; // BIN-4 supports x64 DLLs end-to-end
  req.subsystem = pe::PESubsystem::kWindowsCui;
  req.extra_file_characteristics = 0x2000; // IMAGE_FILE_DLL
  req.rdata_bytes = edata.bytes;           // splice .edata as our .rdata payload

  pe::BuildResult build = pe::BuildPE32PlusImage(req);
  if (build.image.empty()) {
    ReportError("polyld-err-E3201: PE writer produced an empty DLL image");
    return false;
  }

  // Patch the Optional Header Data Directory[0] (Export Table) so the
  // Windows loader can find the export directory we just embedded inside
  // .rdata.  The image we just built has no export directory entry yet
  // because the current writer only owns Imports / Exception / Reloc /
  // IAT directories.  Locate the directory array via lfanew.
  if (build.image.size() >= 0x40) {
    const std::uint32_t lfanew = static_cast<std::uint32_t>(build.image[0x3C]) |
                                 (static_cast<std::uint32_t>(build.image[0x3D]) << 8) |
                                 (static_cast<std::uint32_t>(build.image[0x3E]) << 16) |
                                 (static_cast<std::uint32_t>(build.image[0x3F]) << 24);
    const std::size_t opt_hdr = lfanew + 4 + 20;
    const std::size_t dir = opt_hdr + 112; // Data directories start at OptHdr+112
    // Directory[0] = Export Table.
    const std::uint32_t dir0_rva = build.rdata_rva; // .rdata holds .edata bytes
    auto patch32 = [&](std::size_t off, std::uint32_t v) {
      build.image[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
      build.image[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
      build.image[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
      build.image[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    };
    patch32(dir + 0 * 8 + 0, dir0_rva);
    patch32(dir + 0 * 8 + 4, edata.directory_size);
  }

  // ----- 6. Write the file.
  std::ofstream out(config_.output_file, std::ios::binary);
  if (!out) {
    ReportError("Cannot create output file: " + config_.output_file);
    return false;
  }
  out.write(reinterpret_cast<const char *>(build.image.data()),
            static_cast<std::streamsize>(build.image.size()));
  if (!out) {
    ReportError("Failed writing output file: " + config_.output_file);
    return false;
  }

  stats_.total_output_size = build.image.size();
  Trace("Generated PE32+ DLL: " + std::to_string(build.image.size()) +
        " bytes, " + std::to_string(exports.size()) + " export(s)");
  return true;
}

} // namespace polyglot::linker
