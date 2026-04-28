/**
 * @file     arm64_target_backend.cpp
 * @brief    ITargetBackend adapter for the ARM64 (AArch64) backend
 *
 * Wraps the existing @c Arm64Target code-generation engine in the unified
 * @c polyglot::backends::ITargetBackend interface and self-registers the
 * resulting backend with @c BackendRegistry at static initialisation time.
 *
 * @ingroup  Backend / ARM64
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backends/arm64/include/arm64_target.h"
#include "backends/arm64/include/machine_ir.h"
#include "backends/common/include/backend_registry.h"
#include "backends/common/include/target_backend.h"

namespace polyglot::backends::arm64 {
namespace {

using Clock = std::chrono::steady_clock;

::polyglot::backends::arm64::RegAllocStrategy MapRegAlloc(
    ::polyglot::backends::RegAllocStrategy strategy) {
  switch (strategy) {
  case ::polyglot::backends::RegAllocStrategy::kGraphColoring:
    return ::polyglot::backends::arm64::RegAllocStrategy::kGraphColoring;
  case ::polyglot::backends::RegAllocStrategy::kLinearScan:
  default:
    return ::polyglot::backends::arm64::RegAllocStrategy::kLinearScan;
  }
}

class Arm64TargetBackend final : public ::polyglot::backends::ITargetBackend {
public:
  std::string TargetTriple() const override { return "aarch64-unknown-elf"; }

  std::string Description() const override {
    return "AArch64 (ARM64) backend with linear-scan and graph-coloring "
           "register allocators";
  }

  std::vector<std::string> Aliases() const override {
    return {"arm64", "aarch64", "armv8", "aarch64-apple-darwin",
            "aarch64-linux-gnu", "aarch64-pc-windows-msvc"};
  }

  ::polyglot::backends::BackendCapabilities Capabilities() const override {
    ::polyglot::backends::BackendCapabilities caps;
    caps.emits_object = true;
    caps.emits_assembly = true;
    caps.emits_bitcode = true; // Polyglot bitcode via the default ITargetBackend impl.
    caps.supports_debug_info = true;
    caps.supports_position_independent = true;
    caps.supports_jit = false;
    caps.supports_linear_scan = true;
    caps.supports_graph_coloring = true;
    return caps;
  }

  ::polyglot::backends::CompileResult Compile(
      const ::polyglot::ir::IRContext &module,
      const ::polyglot::backends::TargetOptions &options) override {
    using ::polyglot::backends::BackendDiagnostic;
    using ::polyglot::backends::CompileResult;

    CompileResult result;
    const auto t_start = Clock::now();

    Arm64Target target(&module);
    target.SetRegAllocStrategy(MapRegAlloc(options.reg_alloc));

    if (options.emit == ::polyglot::backends::EmitKind::kBitcode ||
        options.emit == ::polyglot::backends::EmitKind::kLlvmIr) {
      BackendDiagnostic diag;
      diag.severity = BackendDiagnostic::Severity::kError;
      diag.component = "EmitBitcode";
      diag.message = "aarch64 backend does not yet support bitcode/llvm-ir emission";
      result.diagnostics.push_back(std::move(diag));
      result.ok = false;
      return result;
    }

    const auto t_asm0 = Clock::now();
    result.artifacts.assembly_text = target.EmitAssembly();
    const auto t_asm1 = Clock::now();
    result.artifacts.stats.emit_micros +=
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       t_asm1 - t_asm0)
                                       .count());

    if (options.emit == ::polyglot::backends::EmitKind::kAssembly) {
      result.artifacts.stats.total_micros = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t_start).count());
      result.ok = !result.artifacts.assembly_text.empty();
      if (!result.ok) {
        BackendDiagnostic diag;
        diag.severity = BackendDiagnostic::Severity::kError;
        diag.component = "EmitAssembly";
        diag.message = "aarch64 backend produced empty assembly text";
        result.diagnostics.push_back(std::move(diag));
      }
      return result;
    }

    const auto t_obj0 = Clock::now();
    auto mc = target.EmitObjectCode();
    const auto t_obj1 = Clock::now();
    result.artifacts.stats.emit_micros +=
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       t_obj1 - t_obj0)
                                       .count());

    if (mc.sections.empty()) {
      BackendDiagnostic diag;
      diag.severity = BackendDiagnostic::Severity::kError;
      diag.component = "EmitObject";
      diag.message = "aarch64 backend produced no sections";
      result.diagnostics.push_back(std::move(diag));
      result.ok = false;
      return result;
    }

    result.artifacts.sections.reserve(mc.sections.size());
    for (auto &s : mc.sections) {
      ::polyglot::backends::MCSection out;
      out.name = std::move(s.name);
      out.data = std::move(s.data);
      out.is_bss = s.bss;
      result.artifacts.sections.push_back(std::move(out));
    }
    result.artifacts.relocations.reserve(mc.relocs.size());
    for (auto &r : mc.relocs) {
      ::polyglot::backends::MCRelocation out;
      out.section = std::move(r.section);
      out.offset = r.offset;
      out.type = r.type;
      out.symbol = std::move(r.symbol);
      out.addend = r.addend;
      result.artifacts.relocations.push_back(std::move(out));
    }
    result.artifacts.exported_symbols.reserve(mc.symbols.size());
    for (auto &sym : mc.symbols) {
      ::polyglot::backends::MCSymbol out;
      out.name = std::move(sym.name);
      out.section = std::move(sym.section);
      out.value = sym.value;
      out.size = sym.size;
      out.is_global = sym.global;
      out.is_defined = sym.defined;
      if (!out.is_defined && !out.name.empty()) {
        result.artifacts.unresolved_symbols.push_back(out.name);
      }
      result.artifacts.exported_symbols.push_back(std::move(out));
    }

    result.artifacts.stats.total_micros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t_start).count());
    result.ok = true;
    return result;
  }
};

REGISTER_TARGET_BACKEND(std::make_shared<Arm64TargetBackend>());

} // namespace
} // namespace polyglot::backends::arm64
