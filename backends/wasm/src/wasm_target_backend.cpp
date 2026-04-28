/**
 * @file     wasm_target_backend.cpp
 * @brief    ITargetBackend adapter for the WebAssembly backend
 *
 * Wraps the existing @c WasmTarget engine in the unified
 * @c polyglot::backends::ITargetBackend interface and self-registers the
 * resulting backend with @c BackendRegistry at static initialisation time.
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backends/common/include/backend_registry.h"
#include "backends/common/include/target_backend.h"
#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {
namespace {

using Clock = std::chrono::steady_clock;

class WasmTargetBackend final : public ::polyglot::backends::ITargetBackend {
public:
  std::string TargetTriple() const override { return "wasm32-unknown-unknown"; }

  std::string Description() const override {
    return "WebAssembly (wasm32) backend producing self-contained .wasm "
           "binary modules and WAT textual assembly";
  }

  std::vector<std::string> Aliases() const override {
    return {"wasm", "wasm32", "wasm64", "wasm32-wasi", "wasm-unknown-unknown"};
  }

  ::polyglot::backends::BackendCapabilities Capabilities() const override {
    ::polyglot::backends::BackendCapabilities caps;
    caps.emits_object = true;     // WASM module is the "object" form here.
    caps.emits_assembly = true;   // WAT textual form.
    caps.emits_bitcode = true; // Polyglot bitcode via the default ITargetBackend impl.
    caps.supports_debug_info = false; // DWARF-in-WASM not yet wired up.
    caps.supports_position_independent = true;
    caps.supports_jit = false;
    caps.supports_linear_scan = true;
    caps.supports_graph_coloring = false;
    return caps;
  }

  ::polyglot::backends::CompileResult Compile(
      const ::polyglot::ir::IRContext &module,
      const ::polyglot::backends::TargetOptions &options) override {
    using ::polyglot::backends::BackendDiagnostic;
    using ::polyglot::backends::CompileResult;

    CompileResult result;
    const auto t_start = Clock::now();

    if (options.emit == ::polyglot::backends::EmitKind::kBitcode ||
        options.emit == ::polyglot::backends::EmitKind::kLlvmIr) {
      BackendDiagnostic diag;
      diag.severity = BackendDiagnostic::Severity::kError;
      diag.component = "EmitBitcode";
      diag.message = "wasm32 backend does not support bitcode/llvm-ir emission";
      result.diagnostics.push_back(std::move(diag));
      result.ok = false;
      return result;
    }

    WasmTarget target(&module);

    // Always emit the WAT representation so callers can inspect the textual form.
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
        diag.message = "wasm32 backend produced empty WAT text";
        result.diagnostics.push_back(std::move(diag));
      }
      return result;
    }

    // EmitKind::kObject — the WASM backend produces a self-contained binary.
    const auto t_obj0 = Clock::now();
    auto bin = target.EmitWasmBinary();
    const auto t_obj1 = Clock::now();
    result.artifacts.stats.emit_micros +=
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       t_obj1 - t_obj0)
                                       .count());
    if (bin.empty()) {
      BackendDiagnostic diag;
      diag.severity = BackendDiagnostic::Severity::kError;
      diag.component = "EmitObject";
      diag.message = "wasm32 backend produced empty binary";
      result.diagnostics.push_back(std::move(diag));
      result.ok = false;
      return result;
    }

    result.artifacts.object_bytes = std::move(bin);

    // Surface the binary as a single ".text"-equivalent section to keep the
    // generic packaging path symmetric with native targets.
    ::polyglot::backends::MCSection out;
    out.name = ".text";
    out.data = result.artifacts.object_bytes;
    result.artifacts.sections.push_back(std::move(out));

    result.artifacts.stats.total_micros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t_start).count());
    result.ok = true;
    return result;
  }
};

REGISTER_TARGET_BACKEND(std::make_shared<WasmTargetBackend>());

} // namespace
} // namespace polyglot::backends::wasm
