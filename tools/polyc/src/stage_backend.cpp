/**
 * @file     stage_backend.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// stage_backend.cpp — Stage 5 implementation
// ============================================================================

#include "tools/polyc/src/stage_backend.h"

#include <iostream>
#include <sstream>
#include <type_traits>
#include <unordered_map>

#include "backends/arm64/include/arm64_target.h"
#include "backends/wasm/include/wasm_target.h"
#include "backends/x86_64/include/x86_target.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/ssa.h"
#include "middle/include/ir/verifier.h"
#include "middle/include/passes/pass_manager.h"

namespace polyglot::tools {

namespace {

// ── Helper: map a backend MCResult into ObjSection/ObjSymbol vectors ────────

template <typename MCResult>
void AbsorbMC(const MCResult &mc,
              std::vector<ObjSection> &sections,
              std::vector<ObjSymbol>  &symbols) {
    std::unordered_map<std::string, std::uint32_t> sec_index;
    sections.reserve(mc.sections.size());
    for (const auto &sec : mc.sections) {
        ObjSection osec;
        osec.name = sec.name;
        osec.data = sec.data;
        osec.bss  = sec.bss;
        sec_index[sec.name] =
            static_cast<std::uint32_t>(sections.size());
        sections.push_back(std::move(osec));
    }

    // Ensure the entry sentinel is always present
    if (sec_index.count(".text")) {
        symbols.push_back({"_start", sec_index[".text"], 0,
                           sections[sec_index[".text"]].data.size(),
                           true, true});
    }

    std::unordered_map<std::string, std::uint32_t> sym_index;
    for (std::uint32_t i = 0; i < symbols.size(); ++i)
        sym_index[symbols[i].name] = i;

    for (const auto &sym : mc.symbols) {
        if (sym_index.count(sym.name)) continue;
        ObjSymbol osym;
        osym.name = sym.name;
        if (sym.defined && sec_index.count(sym.section)) {
            osym.section_index = sec_index[sym.section];
            osym.defined = true;
        } else {
            osym.section_index = 0xFFFFFFFF;
            osym.defined = false;
        }
        osym.value  = sym.value;
        osym.size   = sym.size;
        osym.global = sym.global;
        sym_index[sym.name] =
            static_cast<std::uint32_t>(symbols.size());
        symbols.push_back(std::move(osym));
    }

    // Attach relocations
    for (const auto &r : mc.relocs) {
        auto s_it = sec_index.find(r.section.empty() ? ".text" : r.section);
        if (s_it == sec_index.end()) continue;
        ObjReloc orr{};
        orr.section_index = s_it->second;
        orr.offset = r.offset;
        orr.type   = r.type;
        orr.addend = r.addend;
        auto sym_it = sym_index.find(r.symbol);
        if (sym_it == sym_index.end()) {
            ObjSymbol ext{};
            ext.name = r.symbol;
            ext.section_index = 0xFFFFFFFF;
            ext.defined = false;
            ext.global  = true;
            sym_index[ext.name] =
                static_cast<std::uint32_t>(symbols.size());
            symbols.push_back(ext);
            sym_it = sym_index.find(r.symbol);
        }
        orr.symbol_index = sym_it->second;
        sections[orr.section_index].relocs.push_back(orr);
    }
}

// ── Helper: apply RegAlloc strategy to a native backend ─────────────────────

void SetRegAlloc(backends::x86_64::X86Target &t, RegAllocChoice c) {
    t.SetRegAllocStrategy(c == RegAllocChoice::kGraphColoring
                              ? backends::x86_64::RegAllocStrategy::kGraphColoring
                              : backends::x86_64::RegAllocStrategy::kLinearScan);
}
void SetRegAlloc(backends::arm64::Arm64Target &t, RegAllocChoice c) {
    t.SetRegAllocStrategy(c == RegAllocChoice::kGraphColoring
                              ? backends::arm64::RegAllocStrategy::kGraphColoring
                              : backends::arm64::RegAllocStrategy::kLinearScan);
}

}  // namespace

// ============================================================================
// RunBackendStage
// ============================================================================

BackendResult RunBackendStage(const DriverSettings &settings,
                               const FrontendResult &frontend,
                               const SemanticResult &semantic,
                               const BridgeResult   &bridge) {
    BackendResult result;
    const bool V = settings.verbose;
    const bool use_arm64 = (settings.arch == "arm64"   ||
                             settings.arch == "aarch64" ||
                             settings.arch == "armv8");
    const bool use_wasm  = (settings.arch == "wasm"   ||
                             settings.arch == "wasm32" ||
                             settings.arch == "wasm64");

    // ── Select / create IR context ───────────────────────────────────────────
    // Non-.ploy: the frontend already produced an IRContext
    std::shared_ptr<ir::IRContext> ir_ctx;
    if (settings.language != "ploy") {
        ir_ctx = frontend.ir_ctx;
        if (!ir_ctx) {
            result.diagnostics.Report(
                core::SourceLoc{"<backend>", 1, 1},
                "no IR context from frontend for language '" + settings.language + "'");
            result.success = false;
            return result;
        }
    } else {
        // .ploy: run lowering now that we have a validated sema instance
        if (!semantic.sema) {
            result.diagnostics.Report(
                core::SourceLoc{"<backend>", 1, 1},
                "semantic stage did not produce a PloySema instance");
            result.success = false;
            return result;
        }
        ir_ctx = std::make_shared<ir::IRContext>();
        ploy::PloyLowering lowering(*ir_ctx, result.diagnostics, *semantic.sema);
        if (!lowering.Lower(frontend.ast) && !settings.force) {
            result.success = false;
            return result;
        }

        // Inject bridge stubs as pre-compiled IR functions
        for (const auto &stub : bridge.stubs) {
            auto fn = ir_ctx->CreateFunction(stub.stub_name);
            fn->is_external    = false;
            fn->is_bridge_stub = true;
            fn->precompiled_code = stub.code;
            for (const auto &r : stub.relocations) {
                ir::StubRelocation sr;
                sr.offset       = static_cast<std::size_t>(r.offset);
                sr.symbol       = r.symbol;
                sr.type         = r.type;
                sr.addend       = r.addend;
                sr.is_pc_relative = r.is_pc_relative;
                sr.size         = static_cast<std::uint8_t>(r.size);
                fn->precompiled_relocs.push_back(sr);
            }
            if (V) std::cerr << "[stage/backend]  stub " << stub.stub_name << "\n";
        }
    }

    // ── SSA + Verify ─────────────────────────────────────────────────────────
    for (auto &fn : ir_ctx->Functions()) {
        ir::ConvertToSSA(*fn);
    }
    {
        std::string verify_msg;
        ir::VerifyOptions vopts;
        // Strict placeholder verification: active in strict mode OR whenever
        // --dev is not set (demand 2026-04-09-12: non-dev mode = -Werror-placeholder-ir)
        vopts.strict = settings.strict || !settings.dev_mode;
        if (!ir::Verify(*ir_ctx, vopts, &verify_msg)) {
            if (settings.strict || !settings.force) {
                result.diagnostics.Report(
                    core::SourceLoc{"<backend>", 1, 1},
                    "IR verification failed: " + verify_msg);
                result.success = false;
                return result;
            }
            std::cerr << "[warn] IR verification: " << verify_msg
                      << " (continuing in --force mode)\n";
        }
    }

    // ── OptLevel → PassManager ────────────────────────────────────────────────
    // PassManager::Build() selects passes based on opt_level; RunOnModule()
    // applies them in the correct order.  This replaces the old hard-wired
    // SSA+Verify sequence that lived directly in driver.cpp.
    if (settings.opt_level > 0) {
        passes::PassManager pm(
            static_cast<passes::PassManager::OptLevel>(settings.opt_level));
        pm.Build();
        std::size_t n = pm.RunOnModule(*ir_ctx, V);
        if (V) std::cerr << "[stage/backend]  " << n << " opt passes/fn\n";

        // Re-verify after optimization
        std::string post_msg;
        ir::VerifyOptions post_vopts;
        post_vopts.strict = settings.strict || !settings.dev_mode;
        if (!ir::Verify(*ir_ctx, post_vopts, &post_msg)) {
            if (settings.strict) {
                result.diagnostics.Report(
                    core::SourceLoc{"<backend>", 1, 1},
                    "post-opt IR verification failed: " + post_msg);
                result.success = false;
                return result;
            }
            if (!settings.force) {
                result.diagnostics.Report(
                    core::SourceLoc{"<backend>", 1, 1},
                    "post-opt IR verification failed: " + post_msg);
                result.success = false;
                return result;
            }
            std::cerr << "[warn] post-opt IR: " << post_msg
                      << " (continuing in --force/dev mode)\n";
        }
    }

    // ── IR text for --emit-ir ────────────────────────────────────────────────
    if (!settings.emit_ir_path.empty()) {
        std::ostringstream oss;
        ir::PrintModule(*ir_ctx, oss);
        result.ir_text = oss.str();
    }

    // ── Code generation ───────────────────────────────────────────────────────
    if (use_wasm) {
        backends::wasm::WasmTarget wasm(ir_ctx.get());
        result.target_triple = wasm.TargetTriple();
        result.assembly_text = wasm.EmitAssembly();
        auto bin = wasm.EmitWasmBinary();
        if (bin.empty()) {
            result.diagnostics.Report(
                core::SourceLoc{"<backend>", 1, 1},
                "WASM backend produced empty binary");
            if (!settings.force) { result.success = false; return result; }
        }
        ObjSection text;
        text.name = ".text";
        text.data = std::move(bin);
        result.sections.push_back(std::move(text));
        result.symbols.push_back(
            {"_start", 0, 0,
             static_cast<std::uint64_t>(result.sections.front().data.size()),
             true, true});
        result.success = true;
        return result;
    }

    auto run_native = [&](auto &target) {
        SetRegAlloc(target, settings.regalloc);
        result.target_triple  = target.TargetTriple();
        result.assembly_text  = target.EmitAssembly();
        auto mc = target.EmitObjectCode();

        if (mc.sections.empty()) {
            if (settings.strict || !settings.force) {
                result.diagnostics.Report(
                    core::SourceLoc{"<backend>", 1, 1},
                    "backend produced no code sections for target '" +
                        result.target_triple + "'");
                result.success = false;
                return;
            }
            // --force / --dev: synthesize a minimal stub
            std::cerr << "[warn] DEGRADED BUILD: synthesizing minimal stub\n";
            using SectionT = typename std::decay_t<decltype(mc.sections)>::value_type;
            SectionT text;
            text.name = ".text";
            if constexpr (std::is_same_v<std::decay_t<decltype(target)>,
                                         backends::x86_64::X86Target>) {
                text.data = {0x31, 0xC0, 0xC3};  // xor eax,eax; ret
            } else {
                text.data = {0x00, 0x00, 0x80, 0xD2,  // mov x0, #0
                             0xC0, 0x03, 0x5F, 0xD6};  // ret
            }
            mc.sections.push_back(text);
            mc.symbols.push_back({"_start", ".text", 0,
                                   static_cast<std::uint64_t>(text.data.size()),
                                   true, true});
            mc.symbols.push_back({"__ploy_degraded_build", ".text", 0, 0, true, false});
        }
        AbsorbMC(mc, result.sections, result.symbols);
        result.success = true;
    };

    if (use_arm64) {
        backends::arm64::Arm64Target arm64(ir_ctx.get());
        run_native(arm64);
    } else {
        backends::x86_64::X86Target x86(ir_ctx.get());
        run_native(x86);
    }

    if (V && result.success) {
        std::cerr << "[stage/backend]  " << result.sections.size()
                  << " section(s), " << result.symbols.size() << " symbol(s)\n";
    }
    return result;
}

}  // namespace polyglot::tools
