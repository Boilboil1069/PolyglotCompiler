/**
 * @file     target_backend.h
 * @brief    Abstract code-generation backend interface
 *
 * Defines the @c ITargetBackend interface that every concrete code-generation
 * backend (x86_64, arm64, wasm, riscv64, ...) must implement, together with
 * the surrounding option / artifact / diagnostic data structures.  Together
 * with @c BackendRegistry this is the single dispatch surface used by the
 * @c polyc and @c polyasm drivers; new backends are added by implementing
 * @c ITargetBackend and self-registering via @c REGISTER_TARGET_BACKEND.
 *
 * Design mirrors @c polyglot::frontends::ILanguageFrontend +
 * @c polyglot::frontends::FrontendRegistry on the frontend side.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "middle/include/ir/ir_context.h"

namespace polyglot::backends {

// ============================================================================
// Strategy / option enums
// ============================================================================

/** @brief Register-allocation strategy requested by the driver. */
enum class RegAllocStrategy {
  kLinearScan,
  kGraphColoring,
};

/** @brief Instruction scheduling strategy requested by the driver. */
enum class SchedulerStrategy {
  kList, // Default list-based scheduler.
  kNone, // Emit in natural block order without reordering.
};

/** @brief MachineIR verifier strictness requested by the driver. */
enum class VerifyLevel {
  kOff,    // Skip verification entirely.
  kOn,     // Run verifier; report errors but continue when --force is set.
  kStrict, // Run verifier; abort compilation on any failure.
};

/** @brief What the backend should emit for the current invocation. */
enum class EmitKind {
  kObject,   // Native relocatable object (sections + relocations).
  kAssembly, // Textual assembly only (no object bytes).
  kBitcode,  // LLVM bitcode (optional, may be unsupported).
  kLlvmIr,   // Textual LLVM IR (optional, may be unsupported).
};

/** @brief Debug-info detail level requested by the driver. */
enum class DebugInfoLevel {
  kNone, // Strip all debug info.
  kLine, // Line-table only.
  kFull, // Full DWARF/PDB with variables and types.
};

// ============================================================================
// Per-invocation options
// ============================================================================

/** @brief Per-invocation backend options forwarded from the driver. */
struct TargetOptions {
  RegAllocStrategy reg_alloc{RegAllocStrategy::kLinearScan};
  SchedulerStrategy scheduler{SchedulerStrategy::kList};
  VerifyLevel verify{VerifyLevel::kOn};
  EmitKind emit{EmitKind::kObject};
  DebugInfoLevel debug_info{DebugInfoLevel::kFull};
  int opt_level{0};
  bool force{false};        // Continue past non-fatal verifier errors.
  bool position_independent{false};
};

// ============================================================================
// Backend metadata
// ============================================================================

/** @brief Capability matrix advertised by a backend. */
struct BackendCapabilities {
  bool emits_object{false};
  bool emits_assembly{false};
  bool emits_bitcode{false};
  bool supports_debug_info{false};
  bool supports_position_independent{false};
  bool supports_jit{false};
  bool supports_graph_coloring{false};
  bool supports_linear_scan{true};
};

/** @brief Public, serialisable description of a registered backend. */
struct BackendInfo {
  std::string triple;
  std::vector<std::string> aliases;
  std::string description;
  BackendCapabilities capabilities;
  bool available{true};
};

// ============================================================================
// Compile-time artifacts produced by a backend
// ============================================================================

/** @brief A single relocation entry exposed in target-neutral form. */
struct MCRelocation {
  std::string section;     // Section the relocation lives in (e.g. ".text").
  std::uint64_t offset{0}; // Byte offset inside the section.
  std::uint32_t type{0};   // Backend-specific relocation type code.
  std::string symbol;      // Target symbol name.
  std::int64_t addend{0};
};

/** @brief A symbol exported (or referenced) by the compiled module. */
struct MCSymbol {
  std::string name;
  std::string section; // Empty when undefined / external.
  std::uint64_t value{0};
  std::uint64_t size{0};
  bool is_global{true};
  bool is_defined{false};
};

/** @brief A raw section emitted by the backend. */
struct MCSection {
  std::string name;
  std::vector<std::uint8_t> data;
  bool is_bss{false};
};

/** @brief Per-stage compile statistics in microseconds. */
struct CompileStats {
  std::uint64_t isel_micros{0};
  std::uint64_t regalloc_micros{0};
  std::uint64_t scheduler_micros{0};
  std::uint64_t emit_micros{0};
  std::uint64_t total_micros{0};
};

/** @brief Aggregate result of a single backend invocation. */
struct TargetArtifacts {
  std::string assembly_text;
  std::vector<MCSection> sections;
  std::vector<MCRelocation> relocations;
  std::vector<MCSymbol> exported_symbols;
  std::vector<std::string> unresolved_symbols;
  // Filled when the backend produces a self-contained binary (e.g. .wasm).
  // For ELF / Mach-O / COFF targets this is left empty: the caller wraps
  // @c sections + @c relocations + @c exported_symbols using the appropriate
  // @c ObjectFileBuilder.
  std::vector<std::uint8_t> object_bytes;
  std::vector<std::uint8_t> bitcode_bytes;
  CompileStats stats;
};

// ============================================================================
// Diagnostics surfaced from a backend
// ============================================================================

/** @brief A single backend-side diagnostic entry. */
struct BackendDiagnostic {
  enum class Severity { kInfo, kWarning, kError };

  Severity severity{Severity::kError};
  std::string message;
  std::string component; // e.g. "isel", "regalloc", "verifier", "EmitObject".
};

/** @brief Outcome of a single @c ITargetBackend::Compile call. */
struct CompileResult {
  TargetArtifacts artifacts;
  std::vector<BackendDiagnostic> diagnostics;
  bool ok{true};
};

// ============================================================================
// ITargetBackend
// ============================================================================

/**
 * @brief Abstract interface implemented by every code-generation backend.
 *
 * Implementations must be reentrant: a single backend instance may be queried
 * for capabilities concurrently and may have @c Compile() invoked from
 * multiple threads provided each call uses a distinct @c IRContext.  All
 * mutable per-invocation state must live in local variables; no per-instance
 * state may carry across @c Compile() calls.
 */
class ITargetBackend {
public:
  virtual ~ITargetBackend() = default;

  // ----- Identification ----------------------------------------------------

  /** @brief Canonical target triple (e.g. "x86_64-unknown-elf"). */
  virtual std::string TargetTriple() const = 0;

  /** @brief Human-friendly description shown by --print-targets. */
  virtual std::string Description() const = 0;

  /** @brief Lower-case aliases accepted by the registry (e.g. "amd64"). */
  virtual std::vector<std::string> Aliases() const { return {}; }

  /** @brief Whether this backend is usable in the current build. */
  virtual bool IsAvailable() const { return true; }

  /** @brief Capability matrix advertised by this backend. */
  virtual BackendCapabilities Capabilities() const = 0;

  // ----- Code generation ---------------------------------------------------

  /**
   * @brief Run the full backend pipeline on @p module.
   *
   * The implementation is expected to honour @p options.emit and populate
   * the corresponding fields of @c CompileResult::artifacts.  Diagnostics
   * are reported via @c CompileResult::diagnostics; on hard failure
   * @c CompileResult::ok must be set to @c false.
   */
  virtual CompileResult Compile(const polyglot::ir::IRContext &module,
                                const TargetOptions &options) = 0;

  // ----- Convenience helpers ----------------------------------------------

  /**
   * @brief Convenience wrapper that calls @c Compile() with
   *        @c EmitKind::kAssembly and returns the textual assembly.
   *        Returns an empty string on failure.
   */
  virtual std::string EmitAssembly(const polyglot::ir::IRContext &module,
                                   const TargetOptions &options);

  /**
   * @brief Convenience wrapper that calls @c Compile() with
   *        @c EmitKind::kObject and returns either the self-contained
   *        @c object_bytes (e.g. wasm) or an empty vector when the caller
   *        is expected to assemble sections itself.  Optional out parameters
   *        receive the relocations and exported symbols.
   */
  virtual std::vector<std::uint8_t> EmitObject(
      const polyglot::ir::IRContext &module, const TargetOptions &options,
      std::vector<MCRelocation> *out_relocs = nullptr,
      std::vector<MCSymbol> *out_symbols = nullptr,
      std::vector<MCSection> *out_sections = nullptr);

  /**
   * @brief Emit LLVM bitcode for @p module.
   *
   * Default implementation reports an "unsupported" diagnostic; backends
   * that opt into bitcode emission must override.  Callers should consult
   * @c Capabilities().emits_bitcode before invoking.
   */
  virtual CompileResult EmitBitcode(const polyglot::ir::IRContext &module,
                                    const TargetOptions &options);
};

// ============================================================================
// Helpers
// ============================================================================

/** @brief Build a @c BackendInfo snapshot from a live backend. */
BackendInfo MakeBackendInfo(const ITargetBackend &backend);

/** @brief Lower-case an ASCII string in-place free helper used by the
 *         registry for case-insensitive triple / alias matching.            */
std::string AsciiToLower(std::string s);

} // namespace polyglot::backends
