#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/lexer_base.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "middle/include/ir/ir_context.h"
#include "tools/polyld/include/polyglot_linker.h"

namespace polyglot::compilation {

// ============================================================================
// Stage 1: Frontend Output
// ============================================================================
// Input: Source file (.ploy, .cpp, .py, etc.)
// Output: Parsed AST with no semantic analysis
// Responsibility: Lexing, parsing, building AST

struct FrontendOutput {
    // The parsed module AST
    std::shared_ptr<ploy::Module> ast;

    // Source metadata
    std::string source_file;
    std::string language;  // "ploy", "cpp", "python", etc.

    // Raw token stream (optional, for debugging)
    std::vector<frontends::Token> tokens;

    // Parse-time diagnostics (errors that prevent semantic analysis)
    std::vector<frontends::Diagnostic> parse_diagnostics;

    // Success flag
    bool success{false};

    // Timestamp for incremental compilation
    std::filesystem::file_time_type source_mtime;
};

// ============================================================================
// Stage 2: Semantic Database
// ============================================================================
// Input: FrontendOutput (parsed AST)
// Output: Validated semantic database with resolved symbols and types
// Responsibility: Type checking, symbol resolution, cross-language validation

struct SemanticDatabase {
    // The validated AST (may have annotations added)
    std::shared_ptr<ploy::Module> validated_ast;

    // Symbol tables
    std::unordered_map<std::string, ploy::PloySymbol> symbols;
    std::unordered_map<std::string, ploy::FunctionSignature> signatures;
    std::unordered_map<std::string, ploy::ForeignClassSchema> class_schemas;

    // Cross-language link entries (validated)
    std::vector<ploy::LinkEntry> link_entries;

    // Type mappings
    std::vector<ploy::TypeMappingEntry> type_mappings;

    // Package information (for imports)
    std::unordered_map<std::string, ploy::PackageInfo> packages;

    // Environment configurations
    std::vector<ploy::VenvConfig> venv_configs;

    // Semantic diagnostics
    std::vector<frontends::Diagnostic> sema_diagnostics;

    // Live sema instance used by lowering and downstream validation.
    std::shared_ptr<ploy::PloySema> sema_instance;

    // Success flag
    bool success{false};
};

// ============================================================================
// Stage 3: Marshal Plan
// ============================================================================
// Input: SemanticDatabase
// Output: Marshalling plan describing how to convert types across boundaries
// Responsibility: Determine marshalling strategies for each cross-language call

enum class MarshalStrategy {
    kDirectCopy,      // Same representation, no conversion needed
    kIntToFloat,      // Integer to floating-point conversion
    kFloatToInt,      // Floating-point to integer conversion
    kStringEncode,    // String encoding conversion (UTF-8, etc.)
    kContainerCopy,   // Container element-by-element copy
    kPointerToHandle, // Wrap pointer in foreign handle
    kStructFieldByField, // Struct marshalling
    kOpaquePtr,       // Opaque pointer pass-through
};

struct ParamMarshalPlan {
    size_t param_index{0};
    core::Type source_type;
    core::Type target_type;
    MarshalStrategy strategy{MarshalStrategy::kDirectCopy};
    size_t size_bytes{0};        // Size of the parameter
    size_t alignment{0};         // Required alignment
    bool needs_heap_alloc{false}; // Whether dynamic allocation is needed
};

struct ReturnMarshalPlan {
    core::Type source_type;
    core::Type target_type;
    MarshalStrategy strategy{MarshalStrategy::kDirectCopy};
    size_t size_bytes{0};
};

struct CallMarshalPlan {
    std::string link_id;         // Unique identifier for this cross-language call
    std::string target_language;
    std::string source_language;
    std::string target_function;
    std::string source_function;

    // Marshalling plans for each parameter
    std::vector<ParamMarshalPlan> param_plans;

    // Marshalling plan for return value
    ReturnMarshalPlan return_plan;

    // Calling convention mismatch info
    bool needs_calling_conv_adapt{false};
    linker::ABIDescriptor target_abi;
    linker::ABIDescriptor source_abi;

    // Estimated cost (for optimization decisions)
    int estimated_cycles{0};
};

struct MarshalPlan {
    // One plan per cross-language link
    std::vector<CallMarshalPlan> call_plans;

    // Global marshalling requirements (shared thunks, etc.)
    std::unordered_map<std::string, std::string> required_helpers;

    // Diagnostics for unsupported type combinations
    std::vector<frontends::Diagnostic> plan_diagnostics;

    // Success flag
    bool success{false};
};

// ============================================================================
// Stage 4: Bridge Generation
// ============================================================================
// Input: MarshalPlan + SemanticDatabase
// Output: Generated bridge/glue code stubs
// Responsibility: Generate actual machine code or IR for marshalling

struct GeneratedStub {
    std::string stub_name;           // Generated symbol name
    std::string link_id;             // References CallMarshalPlan

    // The generated code (machine code or IR)
    std::vector<std::uint8_t> code;

    // Relocations that need to be fixed up
    std::vector<linker::Relocation> relocations;

    // Symbol references
    std::string target_symbol;
    std::string source_symbol;

    // Debug info
    std::string source_location;     // File:line for debugging
};

struct BridgeGenerationOutput {
    // Generated stubs for each cross-language call
    std::vector<GeneratedStub> stubs;

    // Additional symbols to export
    std::vector<std::string> exported_symbols;

    // Required runtime library functions
    std::vector<std::string> runtime_dependencies;

    // Generated metadata (for debugging/introspection)
    std::string bridge_metadata_json;

    // Diagnostics
    std::vector<frontends::Diagnostic> generation_diagnostics;

    // Success flag
    bool success{false};
};

// ============================================================================
// Stage 5: Backend Output
// ============================================================================
// Input: SemanticDatabase + BridgeGenerationOutput
// Output: Target-specific machine code / object files
// Responsibility: Instruction selection, register allocation, code emission

struct CompiledObject {
    std::string name;                // Function/object name
    std::vector<std::uint8_t> code;  // Machine code
    std::vector<std::uint8_t> data;  // Associated data

    // Relocations
    std::vector<linker::Relocation> relocations;

    // Symbol information
    std::vector<linker::Symbol> symbols;

    // Debug information
    std::vector<uint8_t> debug_info;

    // Source mapping (for debugging)
    std::unordered_map<size_t, core::SourceLoc> offset_to_source;
};

struct BackendOutput {
    // One object per function/translation unit
    std::vector<CompiledObject> objects;

    // Target architecture
    std::string target_arch;  // "x86_64", "arm64", "wasm"

    // Target OS
    std::string target_os;    // "linux", "macos", "windows"

    // Backend diagnostics
    std::vector<frontends::Diagnostic> backend_diagnostics;

    // Optional textual assembly emitted by backend.
    std::string assembly_text;

    // Target triple reported by backend.
    std::string target_triple;

    // Success flag
    bool success{false};
};

// ============================================================================
// Stage 6: Packaging Output
// ============================================================================
// Input: BackendOutput (multiple objects)
// Output: Final executable or library
// Responsibility: Linking, section layout, file format emission

enum class OutputFormat {
    kELF,       // Linux
    kMachO,     // macOS
    kPE,        // Windows
    kWasm,      // WebAssembly
    kStaticLib, // .a / .lib
    kSharedLib, // .so / .dylib / .dll
};

struct PackagingOutput {
    // Final binary data
    std::vector<std::uint8_t> binary_data;

    // Output format
    OutputFormat format{OutputFormat::kELF};

    // Output file path (if written)
    std::string output_path;

    // Entry point symbol
    std::string entry_point;

    // Required libraries (for dynamic linking)
    std::vector<std::string> needed_libraries;

    // Exported symbols (for libraries)
    std::vector<std::string> exported_symbols;

    // Packaging diagnostics
    std::vector<frontends::Diagnostic> packaging_diagnostics;

    // Success flag
    bool success{false};

    // File size and checksum
    size_t file_size{0};
    std::string sha256_checksum;
};

// ============================================================================
// Pipeline Context
// ============================================================================
// Holds intermediate results between stages and configuration

struct CompilationContext {
    // Configuration
    struct Config {
        std::string source_file;
        std::string source_text;
        std::string source_language{"ploy"};
        std::string output_file;
        std::string target_arch{"x86_64"};
        std::string target_os;
        std::string mode{"link"};               // compile | assemble | link
        std::string object_format{"pobj"};      // pobj | coff | elf | macho
        std::string polyld_path{"polyld"};
        std::string source_label;
        int opt_level{0};
        bool verbose{false};
        bool strict_mode{false};
        bool force{false};
        std::string aux_dir;
        bool package_index{false};
        int package_index_timeout_ms{30000};
        std::string emit_ir_path;
        std::string emit_asm_path;
        std::string emit_obj_path;
        std::vector<std::string> additional_libs;
        // Path to the serialized cross-language descriptor file written after
        // bridge generation (passed as --ploy-desc to polyld in link mode).
        std::string ploy_desc_file;
    } config;

    // Stage outputs (populated as pipeline progresses)
    std::optional<FrontendOutput> frontend_output;
    std::optional<SemanticDatabase> semantic_db;
    std::optional<MarshalPlan> marshal_plan;
    std::optional<BridgeGenerationOutput> bridge_output;
    std::optional<BackendOutput> backend_output;
    std::optional<PackagingOutput> packaging_output;

    // Shared resources
    std::shared_ptr<frontends::Diagnostics> diagnostics;
    std::shared_ptr<ploy::PackageDiscoveryCache> package_cache;

    // Timing information per stage
    struct StageTiming {
        std::string name;
        double elapsed_ms{0.0};
    };
    std::vector<StageTiming> timings;

    CompilationContext() : diagnostics(std::make_shared<frontends::Diagnostics>()) {}
};

// ============================================================================
// Stage Interfaces
// ============================================================================

class FrontendStage {
  public:
    virtual ~FrontendStage() = default;
    virtual FrontendOutput Run(const CompilationContext::Config& config,
                               frontends::Diagnostics& diagnostics) = 0;
};

class SemanticStage {
  public:
    virtual ~SemanticStage() = default;
    virtual SemanticDatabase Run(const FrontendOutput& input,
                                 frontends::Diagnostics& diagnostics,
                                 const std::shared_ptr<ploy::PackageDiscoveryCache>& cache) = 0;
};

class MarshalPlanStage {
  public:
    virtual ~MarshalPlanStage() = default;
    virtual MarshalPlan Run(const SemanticDatabase& input,
                            frontends::Diagnostics& diagnostics) = 0;
};

class BridgeGenerationStage {
  public:
    virtual ~BridgeGenerationStage() = default;
    virtual BridgeGenerationOutput Run(const MarshalPlan& plan,
                                       const SemanticDatabase& sema_db,
                                       frontends::Diagnostics& diagnostics) = 0;
};

class BackendStage {
  public:
    virtual ~BackendStage() = default;
    virtual BackendOutput Run(const SemanticDatabase& sema_db,
                              const BridgeGenerationOutput& bridges,
                              const CompilationContext::Config& config,
                              frontends::Diagnostics& diagnostics) = 0;
};

class PackagingStage {
  public:
    virtual ~PackagingStage() = default;
    virtual PackagingOutput Run(const BackendOutput& input,
                                const CompilationContext::Config& config,
                                frontends::Diagnostics& diagnostics) = 0;
};

// ============================================================================
// Pipeline Orchestrator
// ============================================================================

class CompilationPipeline {
  public:
    explicit CompilationPipeline(CompilationContext::Config config);

    // Run the complete pipeline
    bool RunAll();

    // Run individual stages (for incremental compilation)
    bool RunFrontend();
    bool RunSemantic();
    bool RunMarshalPlan();
    bool RunBridgeGeneration();
    bool RunBackend();
    bool RunPackaging();

    // Access results
    const CompilationContext& GetContext() const { return context_; }
    PackagingOutput* GetFinalOutput();

    // Access intermediate outputs (for debugging/introspection)
    const FrontendOutput* GetFrontendOutput() const;
    const SemanticDatabase* GetSemanticDb() const;
    const MarshalPlan* GetMarshalPlan() const;
    const BridgeGenerationOutput* GetBridgeOutput() const;
    const BackendOutput* GetBackendOutput() const;

  private:
    CompilationContext context_;

    // Stage implementations (can be mocked for testing)
    std::unique_ptr<FrontendStage> frontend_stage_;
    std::unique_ptr<SemanticStage> semantic_stage_;
    std::unique_ptr<MarshalPlanStage> marshal_plan_stage_;
    std::unique_ptr<BridgeGenerationStage> bridge_generation_stage_;
    std::unique_ptr<BackendStage> backend_stage_;
    std::unique_ptr<PackagingStage> packaging_stage_;
};

// ============================================================================
// Factory Functions
// ============================================================================

// Create default stage implementations
std::unique_ptr<FrontendStage> CreateFrontendStage();
std::unique_ptr<SemanticStage> CreateSemanticStage();
std::unique_ptr<MarshalPlanStage> CreateMarshalPlanStage();
std::unique_ptr<BridgeGenerationStage> CreateBridgeGenerationStage();
std::unique_ptr<BackendStage> CreateBackendStage(const std::string& target_arch);
std::unique_ptr<PackagingStage> CreatePackagingStage();

} // namespace polyglot::compilation
