/**
 * Link-Time Optimization (LTO) support
 *
 * Perform cross-module optimization at link time
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/cfg.h"

namespace polyglot::lto {

// ============ LTO IR representation ============

/**
 * IR used by LTO
 * Includes full module information for cross-module analysis
 */
class LTOModule {
public:
    std::string module_name;
    std::vector<ir::Function> functions;
    std::vector<ir::GlobalVariable> globals;
    
    // Symbol export information
    std::map<std::string, bool> exported_symbols;  // name -> is_public
    
    // Inter-module dependencies
    std::vector<std::string> dependencies;
    
    // Serialize to bitcode
    bool SaveBitcode(const std::string& filename) const;
    bool LoadBitcode(const std::string& filename);
};

/**
 * LTO context - manages all modules participating in the link
 */
class LTOContext {
public:
    // Add a module
    void AddModule(std::unique_ptr<LTOModule> module);
    
    // Get all modules
    const std::vector<std::unique_ptr<LTOModule>>& GetModules() const {
        return modules_;
    }
    
    // Find a function (cross-module)
    ir::Function* FindFunction(const std::string& name);
    
    // Find a global variable
    ir::GlobalVariable* FindGlobal(const std::string& name);
    
    // Build a call graph
    class CallGraph {
    public:
        struct Node {
            std::string function_name;
            std::vector<std::string> callees;
            std::vector<std::string> callers;
        };
        
        std::map<std::string, Node> nodes;
        
        std::vector<std::string> GetRoots() const;
        std::vector<std::string> GetLeaves() const;
    };
    CallGraph BuildCallGraph() const;
    
private:
    std::vector<std::unique_ptr<LTOModule>> modules_;
    std::map<std::string, ir::Function*> function_map_;
    std::map<std::string, ir::GlobalVariable*> global_map_;
};

// ============ LTO optimization passes ============

/**
 * Cross-module inlining
 */
class CrossModuleInliner {
public:
    explicit CrossModuleInliner(LTOContext& context) : context_(context) {}
    
    // Perform cross-module inlining
    void Run();
    
    // Inlining decisions
    struct InlineCandidate {
        std::string caller;
        std::string callee;
        size_t call_site_count;
        size_t callee_size;
        bool should_inline;
    };
    std::vector<InlineCandidate> FindInlineCandidates() const;
    
private:
    LTOContext& context_;
    
    bool ShouldInline(const ir::Function& caller, const ir::Function& callee) const;
    void InlineFunction(ir::Function& caller, const ir::Function& callee);
};

/**
 * Global dead code elimination
 */
class GlobalDeadCodeElimination {
public:
    explicit GlobalDeadCodeElimination(LTOContext& context) : context_(context) {}
    
    // Remove unused functions and global variables
    void Run();
    
    // Mark reachable symbols
    std::set<std::string> MarkReachableSymbols() const;
    
private:
    LTOContext& context_;
};

/**
 * Constant propagation (cross-module)
 */
class InterproceduralConstantPropagation {
public:
    explicit InterproceduralConstantPropagation(LTOContext& context) 
        : context_(context) {}
    
    void Run();
    
private:
    LTOContext& context_;
    
    // Analyze constant arguments
    std::map<std::string, std::vector<ir::Value>> AnalyzeConstantArgs() const;
};

/**
 * Global optimizer (coordinates all LTO optimizations)
 */
class GlobalOptimizer {
public:
    explicit GlobalOptimizer(LTOContext& context) : context_(context) {}
    
    // Run the full LTO optimization pipeline
    void Optimize();
    
    // Individual optimization stages
    void RunInlining();
    void RunDeadCodeElimination();
    void RunConstantPropagation();
    void RunDevirtualization();
    void RunGlobalValueNumbering();
    
    // Optimization statistics
    struct Statistics {
        size_t functions_inlined;
        size_t functions_removed;
        size_t globals_removed;
        size_t virtual_calls_devirtualized;
    };
    Statistics GetStatistics() const { return stats_; }
    
private:
    LTOContext& context_;
    Statistics stats_;
};

// ============ LTO linker integration ============

/**
 * LTO linker
 */
class LTOLinker {
public:
    LTOLinker() = default;
    
    // Add an input object file (contains LTO bitcode)
    void AddInputFile(const std::string& filename);
    
    // Set optimization level
    void SetOptimizationLevel(int level) { opt_level_ = level; }
    
    // Perform LTO link
    bool Link(const std::string& output_file);
    
    // Configuration options
    struct Config {
        int opt_level = 2;
        bool thin_lto = false;      // Thin LTO mode
        bool emit_bitcode = false;   // Emit optimized bitcode
        bool preserve_symbols = false; // Preserve all symbols (for debugging)
        size_t inline_threshold = 225;
        size_t max_function_size = 10000;
    };
    void SetConfig(const Config& config) { config_ = config; }
    
    // Get link statistics
    struct LinkStats {
        size_t modules_linked;
        size_t total_functions;
        size_t optimized_functions;
        double optimization_time_ms;
    };
    LinkStats GetStatistics() const { return stats_; }
    
private:
    std::vector<std::string> input_files_;
    Config config_;
    LinkStats stats_;
    int opt_level_ = 2;
    
    // Load all modules
    bool LoadModules(LTOContext& context);
    
    // Optimization phase
    void OptimizeModules(LTOContext& context);
    
    // Code generation phase
    bool GenerateCode(const LTOContext& context, const std::string& output);
};

// ============ Thin LTO support ============

/**
 * Thin LTO - scalable LTO implementation
 *
 * Compared to traditional LTO, Thin LTO:
 * - Offers better parallelism
 * - Uses less memory
 * - Is friendly to incremental builds
 */
class ThinLTOCodeGenerator {
public:
    // Add a module
    void AddModule(const std::string& identifier, const std::string& bitcode);
    
    // Run Thin LTO
    bool Run();
    
    // Generate a summary index
    struct ModuleSummary {
        std::string module_id;
        std::vector<std::string> defined_symbols;
        std::vector<std::string> referenced_symbols;
        std::map<std::string, size_t> function_sizes;
    };
    std::vector<ModuleSummary> GenerateSummaries() const;
    
    // Import analysis
    std::map<std::string, std::vector<std::string>> ComputeImports() const;
    
    // Optimize in parallel
    void OptimizeInParallel(size_t num_threads);
    
private:
    std::map<std::string, std::string> modules_;  // id -> bitcode
    std::vector<ModuleSummary> summaries_;
};

// ============ Utility functions ============

/**
 * Compile to LTO bitcode
 */
bool CompileToBitcode(const std::string& source_file,
                     const std::string& output_bitcode);

/**
 * Merge bitcode files
 */
bool MergeBitcode(const std::vector<std::string>& input_files,
                 const std::string& output_file);

/**
 * Generate object code from bitcode
 */
bool GenerateObjectFromBitcode(const std::string& bitcode_file,
                              const std::string& output_object);

/**
 * LTO workflow helpers
 */
class LTOWorkflow {
public:
    // Step 1: compile all source files to bitcode
    static bool CompilePhase(const std::vector<std::string>& sources,
                            const std::string& output_dir);
    
    // Step 2: link-time optimization
    static bool LinkPhase(const std::vector<std::string>& bitcodes,
                         const std::string& output_exe);
    
    // Full pipeline
    static bool FullLTO(const std::vector<std::string>& sources,
                       const std::string& output_exe,
                       int opt_level = 2);
};

} // namespace polyglot::lto
