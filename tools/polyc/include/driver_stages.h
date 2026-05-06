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
// driver_stages.h 鈥?Shared data structures for the polyc stage pipeline
//
// Each RunXxxStage() function receives ONLY the output of the previous stage
// (plus the global DriverSettings) and writes its result into the next stage's
// input structure.  No stage is allowed to access driver-global state directly.
//
// Stage flow:
//   ParseArgs() 鈫?DriverSettings
//       鈹斺攢鈻?RunFrontendStage(settings)  鈫?FrontendResult
//             鈹斺攢鈻?RunSemanticStage(settings, frontend) 鈫?SemanticResult
//                   鈹斺攢鈻?RunMarshalStage(settings, semantic) 鈫?MarshalResult
//                         鈹斺攢鈻?RunBridgeStage(settings, marshal, semantic) 鈫?BridgeResult
//                               鈹斺攢鈻?RunBackendStage(settings, bridge, semantic) 鈫?BackendResult
//                                     鈹斺攢鈻?RunPackagingStage(settings, backend) 鈫?int (exit code)
// ============================================================================

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/ir_context.h"

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/language_versions.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polyld/include/linker.h"
#include "common/include/binary_container.h"
#include "common/include/target_triple.h"

namespace polyglot::tools {

// ============================================================================
// DriverSettings 鈥?parsed CLI flags and source metadata
// ============================================================================

/** @brief RegAllocChoice enumeration. */
enum class RegAllocChoice { kLinearScan, kGraphColoring };

/** @brief DriverSettings data structure. */
struct DriverSettings {
  // Source
  std::string source{};      // source text (may be read from file)
  std::string source_path{}; // original file path (empty if inline)
  std::string language{"ploy"};
  bool language_explicit{false};

  // Target 鈥?default architecture matches the host
#if defined(__aarch64__) || defined(_M_ARM64)
  std::string arch{"arm64"};
#else
  std::string arch{"x86_64"};
#endif
  std::string mode{"link"}; // compile | assemble | link

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

  // ---- BIN-7: target triple + container + PE subsystem + entry symbol ----
  // `target_spec` is the verbatim text from `--target=<spec>` (empty when
  // the user did not pass the flag; the driver then auto-fills via
  // `polyglot::common::HostTriple()`).  `target_triple` is the parsed
  // canonical form and is what the rest of the toolchain consumes.
  // `container` is `kAuto` until the driver resolves it from the triple
  // and any explicit `--container=<...>` override.  `subsystem` carries
  // the optional PE subsystem name.  `entry_symbol` carries `--entry=<sym>`.
  std::string                            target_spec{};
  ::polyglot::common::TargetTriple       target_triple{};
  ::polyglot::common::BinaryContainer    container{::polyglot::common::BinaryContainer::kAuto};
  std::string                            subsystem{};
  std::string                            entry_symbol{};

  // Mode flags
  bool force{false};
  bool strict{false};     // reject placeholders, disable --force stubs
  bool permissive{false}; // explicit permissive override
  bool dev_mode{false};   // --dev: retain degraded fallback paths
  bool verbose{true};
  bool emit_aux{true};

  // Optimisation
  int opt_level{0}; // 0-3
  int jobs{1};
  RegAllocChoice regalloc{RegAllocChoice::kLinearScan};

  // PGO (Profile-Guided Optimisation)
  bool pgo_generate{false};   // --pgo-generate: instrument for profiling
  std::string pgo_use_path{}; // --pgo-use <file>: use profile data

  // LTO (Link-Time Optimisation)
  bool lto_enabled{false}; // --lto: enable cross-module LTO

  // Package indexing
  bool package_index{true};
  int package_index_timeout_ms{10000};

  // Include search paths
  std::vector<std::string> include_paths{"."};
  std::vector<std::string> system_include_paths{}; // -isystem
  std::vector<std::string> defines{};              // -DNAME[=VAL]
  std::vector<std::string> undefines{};            // -UNAME

  // Python 鈥?user-supplied .pyi stub roots
  std::vector<std::string> python_stub_paths{}; // --python-stubs

  // Java 鈥?classpath (directories + .jar files)
  std::vector<std::string> classpath{}; // --classpath / -cp

  // .NET 鈥?assembly references
  std::vector<std::string> dotnet_references{}; // --reference / -r

  // Rust 鈥?cargo dir + extern crate mapping
  std::string rust_crate_dir{};                                    // --crate-dir
  std::vector<std::pair<std::string, std::string>> rust_externs{}; // --extern name=path

  // JavaScript / TypeScript 鈥?npm / yarn / pnpm project root and
  // additional `node_modules` roots that the resolver should consult.
  std::string js_project_dir{};                  // --js-project=<dir>
  std::vector<std::string> node_modules_paths{}; // --node-modules=<dir>

  // Ruby 鈥?Bundler project root + extra gem paths.
  std::string ruby_project_dir{};       // --ruby-project=<dir>
  std::vector<std::string> gem_paths{}; // --gem-path=<dir>

  // Go 鈥?module root + extra GOPATH / module-cache hints.
  std::string go_project_dir{};               // --go-project=<dir>
  std::vector<std::string> go_module_paths{}; // --go-mod-cache=<dir>

  // Progress output
  bool progress_json{false}; // --progress=json: emit machine-readable events

  // ----- Token pool (shared frontend lexeme/identifier interning) ---------
  // See docs/realization/token_pool.md.  When dump_token_pool is true the
  // driver writes <stem>.pool_stats.json next to the aux artefacts.
  bool        dump_token_pool{false};        // --dump-token-pool
  bool        token_pool_shared{true};       // settings: frontend.tokenPool.shared
  std::size_t token_pool_arena_chunk_bytes{0}; // 0 = use library default (64 KiB)

  // Incremental compilation cache
  bool clean_cache{false}; // --clean-cache: purge incremental cache

  // ----- Profiling / call-tracing emission --------------------------------
  // See docs/specs/call_graph_schema_en.md and profile_stream_schema_en.md.
  // emit_call_graph_path / emit_profile_symbols_path are activated by
  // --emit=call-graph:<path> and --emit=profile-symbols:<path>.  When
  // profile_instrument is true the middle-end inserts call-trace hooks
  // around every non-bridge function.
  std::string emit_call_graph_path{};
  std::string emit_profile_symbols_path{};
  bool profile_instrument{false};

  // -------------------------------------------------------------------------
  // Per-language version selection.
  //
  // All nine fields default to `kAuto` (= "let the frontend infer").
  // The CLI populates them from `--std=...`, `--python-version=...`,
  // `--java-release=...`, `--cs-lang=...`, `--target-framework=...`,
  // `--rust-edition=...`, `--go-version=...`, `--ecma=...`,
  // `--ruby-version=...`.  They are then forwarded to the matching
  // FrontendOptions field by stage_frontend.
  // -------------------------------------------------------------------------
  frontends::CppDialect            cpp_dialect{frontends::CppDialect::kAuto};
  frontends::PythonVersion         python_version{frontends::PythonVersion::kAuto};
  frontends::JavaRelease           java_release{frontends::JavaRelease::kAuto};
  frontends::DotnetLangVersion     dotnet_lang_version{frontends::DotnetLangVersion::kAuto};
  frontends::DotnetTargetFramework dotnet_target_framework{frontends::DotnetTargetFramework::kAuto};
  frontends::RustEdition           rust_edition{frontends::RustEdition::kAuto};
  frontends::GoVersion             go_version{frontends::GoVersion::kAuto};
  frontends::EcmaVersion           ecma_version{frontends::EcmaVersion::kAuto};
  frontends::RubyVersion           ruby_version{frontends::RubyVersion::kAuto};
};

// ============================================================================
// Object-file primitives shared across backend + packaging
// ============================================================================

/** @brief ObjReloc data structure. */
struct ObjReloc {
  std::uint32_t section_index{0};
  std::uint64_t offset{0};
  std::uint32_t type{1}; // 0 = abs64, 1 = rel32
  std::uint32_t symbol_index{0};
  std::int64_t addend{-4};
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
// Stage 1 鈥?FrontendResult
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
  std::shared_ptr<ir::IRContext> ir_ctx; // non-ploy result; null for .ploy

  // Aux artefacts
  std::string token_dump; // raw token listing for aux file
  std::string ast_dump;   // AST summary for aux file

  // Token-pool diagnostics: serialised JSON dumped by stage_packaging when
  // DriverSettings::dump_token_pool is true.  Empty otherwise.
  std::string token_pool_stats_json;

  // Package discovery cache (built during package-index phase)
  std::shared_ptr<ploy::PackageDiscoveryCache> pkg_cache;

  // Diagnostics accumulated during this stage
  frontends::Diagnostics diagnostics;

  bool success{false};
};

// ============================================================================
// Stage 2 鈥?SemanticResult
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
  std::string symbols_dump; // for aux file

  frontends::Diagnostics diagnostics;
  bool success{false};
};

// ============================================================================
// Stage 3 鈥?MarshalResult
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
  // Optional foreign-language version pin (e.g. "3.11", "c++23"). Empty
  // means the linker / runtime should fall back to the toolchain default.
  std::string lang_version;
};

/** @brief MarshalResult data structure. */
struct MarshalResult {
  std::vector<MarshalCallPlan> call_plans;
  frontends::Diagnostics diagnostics;
  bool success{false};
};

// ============================================================================
// Stage 4 鈥?BridgeResult
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
// Stage 5 鈥?BackendResult
// Produced by: RunBackendStage()
// Contains: object sections + symbols ready for packaging
// ============================================================================

/** @brief BackendResult data structure. */
struct BackendResult {
  std::vector<ObjSection> sections;
  std::vector<ObjSymbol> symbols;
  std::string assembly_text; // textual asm for aux/emit-asm
  std::string target_triple;
  std::string ir_text; // IR text for --emit-ir
  // The fully-lowered IR context kept alive for downstream consumers
  // (call-graph emitter, profile-symbols emitter, instrumentation
  // accountancy).  Null when the backend did not run a lowering step.
  std::shared_ptr<ir::IRContext> ir_ctx;
  frontends::Diagnostics diagnostics;
  bool success{false};
};

} // namespace polyglot::tools
