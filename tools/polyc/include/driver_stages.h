/**
 * @file     driver_stages.h
 * @brief    Compiler driver
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once
// ============================================================================
// driver_stages.h — Shared data structures for the polyc stage pipeline
//
// Each RunXxxStage() function receives ONLY the output of the previous stage
// (plus the global DriverSettings) and writes its result into the next stage's
// input structure.  No stage is allowed to access driver-global state directly.
//
// Stage flow:
//   ParseArgs() → DriverSettings
//       └─▶ RunFrontendStage(settings)  → FrontendResult
//             └─▶ RunSemanticStage(settings, frontend) → SemanticResult
//                   └─▶ RunMarshalStage(settings, semantic) → MarshalResult
//                         └─▶ RunBridgeStage(settings, marshal, semantic) → BridgeResult
//                               └─▶ RunBackendStage(settings, bridge, semantic) → BackendResult
//                                     └─▶ RunPackagingStage(settings, backend) → int (exit code)
// ============================================================================

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "middle/include/ir/ir_context.h"
#include "tools/polyld/include/linker.h"

namespace polyglot::tools {

// ============================================================================
// DriverSettings — parsed CLI flags and source metadata
// ============================================================================

/** @brief RegAllocChoice enumeration. */
enum class RegAllocChoice { kLinearScan, kGraphColoring };

/** @brief DriverSettings data structure. */
struct DriverSettings {
    // Source
    std::string source{};           // source text (may be read from file)
    std::string source_path{};      // original file path (empty if inline)
    std::string language{"ploy"};
    bool language_explicit{false};

    // Target — default architecture matches the host
#if defined(__aarch64__) || defined(_M_ARM64)
    std::string arch{"arm64"};
#else
    std::string arch{"x86_64"};
#endif
    std::string mode{"link"};       // compile | assemble | link

    // Object format
#if defined(_WIN32)
    std::string obj_format{"coff"};
#elif defined(__APPLE__)
    std::string obj_format{"macho"};
#else
    std::string obj_format{"elf"};
#endif

    // Output paths
    std::string output{"a.out"};
    std::string emit_obj_path{};
    std::string emit_asm_path{};
    std::string emit_ir_path{};

    // Toolchain
    std::string polyld_path{"polyld"};

    // Mode flags
    bool force{false};
    bool strict{false};         // reject placeholders, disable --force stubs
    bool permissive{false};     // explicit permissive override
    bool dev_mode{false};       // --dev: retain degraded fallback paths
    bool verbose{true};
    bool emit_aux{true};

    // Optimisation
    int opt_level{0};           // 0-3
    int jobs{1};
    RegAllocChoice regalloc{RegAllocChoice::kLinearScan};

    // PGO (Profile-Guided Optimisation)
    bool pgo_generate{false};        // --pgo-generate: instrument for profiling
    std::string pgo_use_path{};      // --pgo-use <file>: use profile data

    // LTO (Link-Time Optimisation)
    bool lto_enabled{false};         // --lto: enable cross-module LTO

    // Package indexing
    bool package_index{true};
    int package_index_timeout_ms{10000};

    // Per-language preprocessing overrides
    std::unordered_map<std::string, bool> pp_overrides{
        {"cpp", true},
        {"rust", false},
        {"python", false},
    };

    // Include search paths
    std::vector<std::string> include_paths{"."};
};

// ============================================================================
// Object-file primitives shared across backend + packaging
// ============================================================================

/** @brief ObjReloc data structure. */
struct ObjReloc {
    std::uint32_t section_index{0};
    std::uint64_t offset{0};
    std::uint32_t type{1};          // 0 = abs64, 1 = rel32
    std::uint32_t symbol_index{0};
    std::int64_t  addend{-4};
};

/** @brief ObjSection data structure. */
struct ObjSection {
    std::string name;
    std::vector<std::uint8_t> data;
    bool bss{false};
    std::vector<ObjReloc> relocs;
};

/** @brief ObjSymbol data structure. */
struct ObjSymbol {
    std::string name;
    std::uint32_t section_index{0xFFFFFFFF};
    std::uint64_t value{0};
    std::uint64_t size{0};
    bool global{true};
    bool defined{false};
};

// ============================================================================
// Stage 1 — FrontendResult
// Produced by: RunFrontendStage()
// Contains: preprocessed source, tokens, AST (for .ploy); raw IR for others
// ============================================================================

/** @brief FrontendResult data structure. */
struct FrontendResult {
    // Shared across all languages
    std::string language;
    std::string source_label;     // diagnostic label (filename or "<cli>")
    std::string processed_source; // preprocessed text

    // .ploy-specific (null for non-ploy languages)
    std::shared_ptr<ploy::Module> ast;
    std::vector<frontends::Token> tokens;

    // Non-ploy: IR is produced directly in the frontend stage
    std::shared_ptr<ir::IRContext> ir_ctx;  // non-ploy result; null for .ploy

    // Aux artefacts
    std::string token_dump;       // raw token listing for aux file
    std::string ast_dump;         // AST summary for aux file

    // Package discovery cache (built during package-index phase)
    std::shared_ptr<ploy::PackageDiscoveryCache> pkg_cache;

    // Diagnostics accumulated during this stage
    frontends::Diagnostics diagnostics;

    bool success{false};
};

// ============================================================================
// Stage 2 — SemanticResult
// Produced by: RunSemanticStage()
// Contains: validated AST, symbol table, signatures, link entries
// ============================================================================

/** @brief SemanticResult data structure. */
struct SemanticResult {
    // Sema instance (kept alive for lowering in bridge/backend stages)
    std::shared_ptr<ploy::PloySema> sema;

    // Symbol metadata
    std::unordered_map<std::string, ploy::PloySymbol> symbols;
    std::unordered_map<std::string, ploy::FunctionSignature> signatures;
    std::vector<ploy::LinkEntry> link_entries;

    // IR dump
    std::string symbols_dump;     // for aux file

    frontends::Diagnostics diagnostics;
    bool success{false};
};

// ============================================================================
// Stage 3 — MarshalResult
// Produced by: RunMarshalStage()
// Contains: cross-language parameter conversion plan
// ============================================================================

/** @brief MarshalCallPlan data structure. */
struct MarshalCallPlan {
    std::string link_id;
    std::string source_language;
    std::string target_language;
    std::string source_function;
    std::string target_function;
    // Param descriptors (one per parameter crossing the language boundary)
    std::size_t param_count{0};
};

/** @brief MarshalResult data structure. */
struct MarshalResult {
    std::vector<MarshalCallPlan> call_plans;
    frontends::Diagnostics diagnostics;
    bool success{false};
};

// ============================================================================
// Stage 4 — BridgeResult
// Produced by: RunBridgeStage()
// Contains: resolved stub IR functions ready for injection into the IR module
// ============================================================================

/** @brief BridgeStubEntry data structure. */
struct BridgeStubEntry {
    std::string stub_name;
    std::vector<std::uint8_t> code;
    std::vector<linker::Relocation> relocations;
};

/** @brief BridgeResult data structure. */
struct BridgeResult {
    std::vector<BridgeStubEntry> stubs;
    // Path to the serialized descriptor file (written to aux dir)
    std::string descriptor_file;
    frontends::Diagnostics diagnostics;
    bool success{false};
};

// ============================================================================
// Stage 5 — BackendResult
// Produced by: RunBackendStage()
// Contains: object sections + symbols ready for packaging
// ============================================================================

/** @brief BackendResult data structure. */
struct BackendResult {
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    std::string assembly_text;    // textual asm for aux/emit-asm
    std::string target_triple;
    std::string ir_text;          // IR text for --emit-ir
    frontends::Diagnostics diagnostics;
    bool success{false};
};

}  // namespace polyglot::tools
