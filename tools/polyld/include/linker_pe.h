/**
 * @file     linker_pe.h
 * @brief    PE-specific linker support: .def parsing, export-table layout,
 *           CLI /EXPORT merging, and PE base-relocation translation.
 *
 * This header complements the standalone PE32+ writer in
 * `tools/polyld/include/pe_writer.h`.  The writer is byte-shape only; the
 * routines here are the *linker*-side glue that decides which symbols to
 * export, where to source export descriptors from (command line vs `.def`
 * file vs future `__declspec(dllexport)` attributes), and how to translate
 * the linker's neutral `Relocation` records into the PE flavour the writer
 * accepts.
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-05-05
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tools/polyld/include/linker.h"
#include "tools/polyld/include/pe_writer.h"

namespace polyglot::linker::pe {

// ===========================================================================
// LinkerExport
// ===========================================================================

/// One DLL export record in the linker's neutral form.  Populated by either
/// the command-line `/EXPORT:` flag or by `.def`-file parsing; merged into
/// a single de-duplicated table by `MergeExports` and then translated into
/// PE on-disk bytes by `BuildExportSection`.
///
/// `rva` is the 32-bit RVA (relative to the produced image's base) of the
/// exported entity; the linker fills it in once symbol addresses are known.
/// `ordinal` is 1-based; `0` means "auto-assign at table build time" so
/// callers that only care about by-name lookup can leave the field zero.
struct LinkerExport {
  std::string name;        ///< Public name as seen by `GetProcAddress`.
  std::string internal;    ///< Optional internal symbol when `name` is an alias.
  std::uint32_t rva{0};    ///< RVA of the entity (filled in by the linker).
  std::uint16_t ordinal{0}; ///< 1-based ordinal; 0 ⇒ auto-assign sequentially.
  bool noname{false};      ///< When true, omit the export from the name table.
  bool data{false};        ///< When true, the export is data (not a function).
};

/// Origin tag used by `MergeExports` for diagnostic provenance.
enum class ExportSource {
  kCommandLine, ///< Originated from `/EXPORT:` on the polyld command line.
  kDefFile,     ///< Originated from a `.def` file via `ParseDefFile`.
  kDeclSpec,    ///< Originated from a `__declspec(dllexport)` attribute.
};

// ===========================================================================
// .def file parsing + export merging
// ===========================================================================

/// Parse a Microsoft module-definition (`.def`) file from disk.  Recognises
/// the `LIBRARY <name>` and `EXPORTS` sections; any other top-level
/// statements are tolerated and ignored so that vendor tooling keeps
/// working.  Lines beginning with `;` or `#` are treated as comments.
///
/// The grammar accepted in `EXPORTS` is the standard subset:
///
/// ```
///   <public>[=<internal>] [@<ordinal> [NONAME]] [DATA]
/// ```
///
/// Examples:
///
/// ```
///   Add
///   AddPair = Add @ 1
///   _Sub@8 = sub_impl
///   GlobalData DATA
/// ```
///
/// Returns `true` on success.  `library_out` receives the value of the
/// `LIBRARY` directive (empty when omitted).  `errors_out` collects parse
/// errors keyed by `polyld-err-E3200` (malformed `.def` file).
bool ParseDefFile(const std::string &path,
                  std::string &library_out,
                  std::vector<LinkerExport> &exports_out,
                  std::vector<std::string> &errors_out);

/// Parse the value of one `/EXPORT:` command-line flag into a LinkerExport.
/// Accepts the same right-hand-side grammar as the `.def` `EXPORTS` body
/// (`name[=internal][,@ordinal][,NONAME][,DATA]`).  Returns true on success.
bool ParseCliExportSpec(const std::string &spec, LinkerExport &out,
                        std::vector<std::string> &errors_out);

/// Merge `incoming` into `into` deduplicating by public name.  Conflicts —
/// same public `name` but different `internal` / `ordinal` / flag bits —
/// are reported via `errors_out` as `polyld-err-E3201` and the function
/// returns `false`.  Identical duplicate entries are silently coalesced.
bool MergeExports(std::vector<LinkerExport> &into,
                  const std::vector<LinkerExport> &incoming,
                  ExportSource source,
                  std::vector<std::string> &errors_out);

// ===========================================================================
// Export section (.edata) layout
// ===========================================================================

/// Result of the export-section builder.
struct ExportSectionResult {
  std::vector<std::uint8_t> bytes; ///< .edata raw bytes ready to splice in.
  std::uint32_t directory_rva{0};  ///< RVA of the IMAGE_EXPORT_DIRECTORY.
  std::uint32_t directory_size{0}; ///< Size of the export directory subtree.
};

/// Build a complete PE export section (`.edata`) from a vector of exports
/// already enriched with their final `rva` values.  Layout produced
/// (contiguous, in this order):
///
///   [IMAGE_EXPORT_DIRECTORY]                      -- 40 bytes
///   [Export Address Table]   (4 bytes per entry, indexed by ordinal-Base)
///   [Name Pointer Table]     (4 bytes per name, sorted lexicographically)
///   [Ordinal Table]          (2 bytes per name, 1:1 with Name Pointer Table)
///   [DLL name]               (ASCIZ)
///   [Export name strings]    (ASCIZ each)
///
/// Ordinals are auto-assigned when zero: starting from `1` and skipping
/// values already claimed by explicit ordinals.  When the explicit ordinal
/// set creates gaps the EAT is padded with zero entries (per spec).  All
/// RVAs in the produced bytes are absolute (relative to the image base);
/// `edata_rva` is the RVA at which the produced bytes will be laid down.
ExportSectionResult
BuildExportSection(const std::vector<LinkerExport> &exports,
                   const std::string &dll_name,
                   std::uint32_t edata_rva);

// ===========================================================================
// Relocation translation (BIN-4 item 3)
// ===========================================================================

/// Light-weight description of a single relocation surviving into the final
/// image.  This is the input the linker hands to the PE base-relocation
/// translator; intentionally smaller than the full `polyglot::linker::
/// Relocation` so the translator can be unit-tested without instantiating
/// an entire `Linker`.
struct PendingRelocation {
  std::uint32_t rva{0};  ///< RVA of the location whose bytes need patching.
  std::uint32_t type{0}; ///< Architecture-specific relocation type code.
  bool is_pc_relative{false}; ///< When true, no PE base-reloc is required.
};

/// Translate a list of `PendingRelocation`s for the supplied target
/// `machine` into PE base-relocation entries suitable for splicing into a
/// `BuildRequest::base_relocations` vector.
///
/// Behaviour:
///   * `is_pc_relative == true` is a no-op (PE base relocs only fix
///     absolute addresses).
///   * x86_64 absolute 64-bit (`R_X86_64_64`)            ⇒ DIR64 (type 10)
///   * x86_64 absolute 32-bit / 32S                       ⇒ HIGHLOW (type 3)
///   * arm64 absolute 64-bit (`R_AARCH64_ABS64`)          ⇒ DIR64 (type 10)
///   * arm64 ADRP / ADD imm12 / LDST imm12                ⇒ ARM64 page family
///   * Anything else (including ELF GOT/PLT codes that do not map to any
///     PE base-relocation type) is reported via `errors_out` as
///     `polyld-err-E3210` and the function returns `false`.  Translation
///     of the surviving entries still completes so callers can surface
///     the full conflict set in one pass.
bool TranslateRelocationsToPEBaseRelocs(
    const std::vector<PendingRelocation> &input,
    PEMachine machine,
    std::vector<BaseRelocation> &out,
    std::vector<std::string> &errors_out);

} // namespace polyglot::linker::pe
