/**
 * @file     linker_probe.h
 * @brief    Probe-then-invoke helpers for selecting an available system linker
 *
 * These helpers were introduced in v1.4.1 to fix the long-standing issue
 * where polyc unconditionally invoked `link.exe` (and then `lld-link`) on
 * Windows even when neither was on PATH, causing CMD itself to print
 * `'link' is not recognized as an internal or external command` to stderr.
 *
 * The helpers are deliberately format-agnostic and shared between the
 * staged compilation pipeline (tools/polyc/src/compilation_pipeline.cpp)
 * and the legacy single-pass driver (tools/polyc/src/stage_packaging.cpp).
 *
 * Probe rules:
 *   - `IsExecutableOnPath()` first stat-checks the literal input (handles
 *     absolute/relative paths to a sibling polyld), then on Windows tries
 *     the `.exe` suffix, then runs `where` (Windows) / `command -v` (POSIX)
 *     with stdout/stderr both redirected to the platform null sink so the
 *     probe itself never produces visible output.
 *
 * Selection rules per object format:
 *   - pobj  : bundled `polyld` (canonical consumer); no native fallback.
 *   - coff  : MSVC `link` -> LLVM `lld-link` -> bundled `polyld`.
 *   - macho : `clang` -> `ld` -> bundled `polyld`.
 *   - elf   : `clang` -> `gcc` -> `ld` -> bundled `polyld`.
 *
 * The bundled `polyld` is the universal fallback because its loader
 * (tools/polyld/src/linker.cpp) detects COFF / ELF / Mach-O input by magic
 * bytes and produces a matching native executable, so a polyc invocation
 * can always link successfully on a host that ships only the polyglot
 * toolchain itself.
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-28
 */

#pragma once

#include <string>

namespace polyglot::tools::linker_probe {

/// Return true if the named executable can be resolved on the host PATH or
/// is an already-existing absolute / relative path on disk.  Never produces
/// visible stdout/stderr output.  Empty input returns false.
bool IsExecutableOnPath(const std::string &exe);

/// Quote a path for the host shell when it contains a space.  Other
/// characters are passed through unchanged because the consumer commands
/// (cl/link/clang/polyld) accept them verbatim on both Windows CMD and
/// POSIX shells.
std::string ShellQuote(const std::string &p);

/// Result of linker selection.  An empty `command_template` signals that
/// no candidate is available for the requested format and the caller should
/// keep the .obj file and emit a structured diagnostic.
struct LinkerChoice {
  std::string display_name;     ///< Short label for verbose logging.
  std::string command_template; ///< Contains {OBJ} / {OUT} placeholders.
};

/// Walk the priority list for the given object format and return the first
/// available linker.  `polyld_path` is the resolved absolute or sibling
/// path to the bundled polyld executable; it is consulted both for the
/// pobj-direct case and as the universal fallback for native formats.
LinkerChoice SelectAvailableLinker(const std::string &format, const std::string &polyld_path);

/// Substitute {OBJ} / {OUT} placeholders in `choice.command_template` and
/// append optional `--ploy-desc` / `--aux-dir` flags (only meaningful when
/// the chosen linker is polyld; ignored otherwise).
std::string ExpandLinkCommand(const LinkerChoice &choice, const std::string &obj_path,
                              const std::string &out_path, const std::string &ploy_desc_file,
                              const std::string &aux_dir);

} // namespace polyglot::tools::linker_probe
