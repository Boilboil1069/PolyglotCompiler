/**
 * @file     target_backend.cpp
 * @brief    Default helpers for ITargetBackend
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/common/include/target_backend.h"

#include <algorithm>
#include <cctype>

namespace polyglot::backends {

// ============================================================================
// AsciiToLower
// ============================================================================

std::string AsciiToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

// ============================================================================
// MakeBackendInfo
// ============================================================================

BackendInfo MakeBackendInfo(const ITargetBackend &backend) {
  BackendInfo info;
  info.triple = backend.TargetTriple();
  info.aliases = backend.Aliases();
  info.description = backend.Description();
  info.capabilities = backend.Capabilities();
  info.available = backend.IsAvailable();
  return info;
}

// ============================================================================
// ITargetBackend convenience helpers
// ============================================================================

std::string ITargetBackend::EmitAssembly(const polyglot::ir::IRContext &module,
                                         const TargetOptions &options) {
  TargetOptions local = options;
  local.emit = EmitKind::kAssembly;
  CompileResult result = Compile(module, local);
  if (!result.ok) {
    return {};
  }
  return std::move(result.artifacts.assembly_text);
}

std::vector<std::uint8_t> ITargetBackend::EmitObject(
    const polyglot::ir::IRContext &module, const TargetOptions &options,
    std::vector<MCRelocation> *out_relocs, std::vector<MCSymbol> *out_symbols,
    std::vector<MCSection> *out_sections) {
  TargetOptions local = options;
  local.emit = EmitKind::kObject;
  CompileResult result = Compile(module, local);
  if (!result.ok) {
    return {};
  }
  if (out_relocs) {
    *out_relocs = std::move(result.artifacts.relocations);
  }
  if (out_symbols) {
    *out_symbols = std::move(result.artifacts.exported_symbols);
  }
  if (out_sections) {
    *out_sections = std::move(result.artifacts.sections);
  }
  return std::move(result.artifacts.object_bytes);
}

CompileResult ITargetBackend::EmitBitcode(const polyglot::ir::IRContext & /*module*/,
                                          const TargetOptions & /*options*/) {
  // Default policy: backends that do not advertise emits_bitcode in their
  // capability matrix are reported as unsupported.  This keeps the public
  // contract honest until the bitcode emitter ships in a later milestone.
  CompileResult result;
  result.ok = false;
  BackendDiagnostic diag;
  diag.severity = BackendDiagnostic::Severity::kError;
  diag.component = "EmitBitcode";
  diag.message = TargetTriple() + " backend does not support bitcode emission";
  result.diagnostics.push_back(std::move(diag));
  return result;
}

} // namespace polyglot::backends
