/**
 * Link-Time Optimization (LTO) support
 *
 * Perform cross-module optimization at link time including:
 * - Cross-module inlining with cost-benefit analysis
 * - Interprocedural constant propagation
 * - Global dead code elimination
 * - Devirtualization
 * - Global value numbering
 * - Thin LTO for scalable builds
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/cfg.h"

namespace polyglot::lto {

// ============ Forward declarations ============
class LTOContext;
class GlobalOptimizer;

// ============ Inlining cost model ============

/**
 * Cost model for inline decisions
 * Considers instruction count, call sites, and optimization opportunities
 */
struct InlineCostModel {
    // Base cost of inlining (instruction count multiplier)
    static constexpr size_t kBaseInstructionCost = 5;
    
    // Bonus for small functions
    static constexpr size_t kSmallFunctionBonus = 50;
    static constexpr size_t kSmallFunctionThreshold = 10;
    
    // Bonus for functions called only once
    static constexpr size_t kSingleCallSiteBonus = 75;
    
    // Bonus for hot call sites (from PGO data)
    static constexpr size_t kHotCallSiteBonus = 100;
    
    // Penalty for recursive functions
    static constexpr size_t kRecursivePenalty = 200;
    
    // Penalty for functions with many basic blocks
    static constexpr size_t kComplexityPenalty = 2;
    
    // Maximum cost to allow inlining
    static constexpr size_t kDefaultInlineThreshold = 225;
};

// ============ LTO IR representation ============

/**
 * IR used by LTO
 * Includes full module information for cross-module analysis
 */
class LTOModule {
public:
    std::string module_name;
    std::vector<ir::Function> functions;
    std::vector<ir::GlobalValue> globals;
    
    // Symbol export information
    std::map<std::string, bool> exported_symbols;  // name -> is_public
    
    // Inter-module dependencies
    std::vector<std::string> dependencies;
    
    // Entry points (main, exported functions)
    std::set<std::string> entry_points;
    
    // Serialize to bitcode
    bool SaveBitcode(const std::string& filename) const;
    bool LoadBitcode(const std::string& filename);
    
    // Get function by name
    ir::Function* GetFunction(const std::string& name);
    const ir::Function* GetFunction(const std::string& name) const;
    
    // Calculate total instruction count
    size_t GetTotalInstructionCount() const;
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

    // Mutable access for transformation passes
    std::vector<std::unique_ptr<LTOModule>>& MutableModules() {
        return modules_;
    }
    
    // Find a function (cross-module)
    ir::Function* FindFunction(const std::string& name);
    const ir::Function* FindFunction(const std::string& name) const;
    
    // Find a global variable
    ir::GlobalValue* FindGlobal(const std::string& name);
    const ir::GlobalValue* FindGlobal(const std::string& name) const;

    // Refresh lookup tables after transformations
    void RebuildIndexes();
    
    // Get all entry points across modules
    std::set<std::string> GetEntryPoints() const;
    
    // Build a call graph
    class CallGraph {
    public:
        struct CallSite {
            std::string caller;
            std::string callee;
            size_t call_count{1};     // Number of times this edge appears
            bool is_indirect{false};   // Indirect call through pointer
            bool is_hot{false};        // Hot call site from PGO
        };
        
        struct Node {
            std::string function_name;
            std::vector<std::string> callees;
            std::vector<std::string> callers;
            size_t instruction_count{0};
            size_t block_count{0};
            bool is_recursive{false};
            bool is_entry_point{false};
        };
        
        std::map<std::string, Node> nodes;
        std::vector<CallSite> call_sites;
        
        std::vector<std::string> GetRoots() const;
        std::vector<std::string> GetLeaves() const;
        
        // Get strongly connected components (for recursion detection)
        std::vector<std::vector<std::string>> GetSCCs() const;
        
        // Get reverse post-order for bottom-up traversal
        std::vector<std::string> GetReversePostOrder() const;
        
        // Check if callee is reachable from caller
        bool IsReachable(const std::string& from, const std::string& to) const;
    };
    CallGraph BuildCallGraph() const;
    
private:
    std::vector<std::unique_ptr<LTOModule>> modules_;
    std::map<std::string, ir::Function*> function_map_;
    std::map<std::string, ir::GlobalValue*> global_map_;
};

// ============ LTO optimization passes ============

/**
 * Lattice value for constant propagation
 * Represents the constant state of a value: top (unknown), constant, or bottom (varying)
 */
struct LatticeValue {
    enum class Kind {
        kTop,       // Unknown (not yet analyzed)
        kConstant,  // Known constant value
        kBottom     // Varying (not constant)
    };
    
    Kind kind{Kind::kTop};
    int64_t int_value{0};
    double float_value{0.0};
    bool is_float{false};
    
    static LatticeValue Top() { return {Kind::kTop, 0, 0.0, false}; }
    static LatticeValue Bottom() { return {Kind::kBottom, 0, 0.0, false}; }
    static LatticeValue Constant(int64_t v) { return {Kind::kConstant, v, 0.0, false}; }
    static LatticeValue Constant(double v) { return {Kind::kConstant, 0, v, true}; }
    static LatticeValue Constant(int v) { return Constant(static_cast<int64_t>(v)); }
    
    bool IsTop() const { return kind == Kind::kTop; }
    bool IsBottom() const { return kind == Kind::kBottom; }
    bool IsConstant() const { return kind == Kind::kConstant; }
    
    // Meet operation for lattice
    LatticeValue Meet(const LatticeValue& other) const;
    
    bool operator==(const LatticeValue& other) const;
    bool operator!=(const LatticeValue& other) const { return !(*this == other); }
};

/**
 * Cross-module inlining with cost-benefit analysis
 */
class CrossModuleInliner {
public:
    explicit CrossModuleInliner(LTOContext& context, size_t threshold = InlineCostModel::kDefaultInlineThreshold) 
        : context_(context), inline_threshold_(threshold) {}
    
    // Perform cross-module inlining
    void Run();
    
    // Inlining decisions
    struct InlineCandidate {
        std::string caller;
        std::string callee;
        size_t call_site_count{0};
        size_t callee_size{0};
        size_t callee_block_count{0};
        size_t inline_cost{0};
        size_t inline_benefit{0};
        bool is_recursive{false};
        bool is_hot{false};
        bool should_inline{false};
    };
    std::vector<InlineCandidate> FindInlineCandidates() const;
    
    // Get inlining statistics
    size_t GetInlinedCount() const { return inlined_count_; }
    
    // Set inline threshold
    void SetThreshold(size_t threshold) { inline_threshold_ = threshold; }
    
private:
    friend class GlobalOptimizer;
    LTOContext& context_;
    size_t inline_threshold_;
    size_t inlined_count_{0};
    
    // Calculate instruction count for a function
    size_t CalculateFunctionSize(const ir::Function& func) const;
    
    // Calculate inline cost using cost model
    size_t CalculateInlineCost(const ir::Function& caller, 
                               const ir::Function& callee,
                               size_t call_site_count,
                               bool is_recursive) const;
    
    // Calculate benefit from inlining (optimization opportunities)
    size_t CalculateInlineBenefit(const ir::Function& caller,
                                  const ir::Function& callee) const;
    
    bool ShouldInline(const ir::Function& caller, const ir::Function& callee) const;
    void InlineFunction(ir::Function& caller, const ir::Function& callee);
    
    // Clone and rename instructions from callee
    void CloneInstructions(ir::BasicBlock& target, 
                          const ir::BasicBlock& source,
                          const std::string& prefix,
                          std::map<std::string, std::string>& name_map);
};

/**
 * Global dead code elimination
 * Removes unreachable functions and globals based on call graph analysis
 */
class GlobalDeadCodeElimination {
public:
    explicit GlobalDeadCodeElimination(LTOContext& context) : context_(context) {}
    
    // Remove unused functions and global variables
    void Run();
    
    // Mark reachable symbols starting from entry points
    std::set<std::string> MarkReachableSymbols() const;
    
    // Get elimination statistics
    size_t GetRemovedFunctions() const { return removed_functions_; }
    size_t GetRemovedGlobals() const { return removed_globals_; }
    
private:
    LTOContext& context_;
    size_t removed_functions_{0};
    size_t removed_globals_{0};
    
    // Recursively mark reachable symbols
    void MarkReachable(const std::string& symbol,
                      std::set<std::string>& reachable,
                      const LTOContext::CallGraph& cg) const;
};

/**
 * Interprocedural constant propagation
 * Propagates constant arguments across function boundaries
 */
class InterproceduralConstantPropagation {
public:
    explicit InterproceduralConstantPropagation(LTOContext& context) 
        : context_(context) {}
    
    void Run();
    
    // Get propagation statistics
    size_t GetConstantsPropagated() const { return constants_propagated_; }
    size_t GetArgumentsSpecialized() const { return arguments_specialized_; }
    
    // Analyze which function arguments are always called with constants
    struct ConstantArgInfo {
        std::string function_name;
        size_t arg_index;
        LatticeValue value;
    };
    std::vector<ConstantArgInfo> AnalyzeConstantArgs() const;
    
private:
    LTOContext& context_;
    size_t constants_propagated_{0};
    size_t arguments_specialized_{0};
    
    // Lattice values for each variable
    std::map<std::string, LatticeValue> lattice_;
    
    // Propagate constants within a function
    void PropagateInFunction(ir::Function& func);
    
    // Evaluate a binary operation on lattice values
    LatticeValue EvaluateBinaryOp(ir::BinaryInstruction::Op op,
                                  const LatticeValue& lhs,
                                  const LatticeValue& rhs) const;
    
    // Replace uses of constant values
    void ReplaceConstantUses(ir::Function& func);
};

/**
 * Cross-module devirtualization
 * Resolves virtual calls when the concrete type is known
 */
class CrossModuleDevirtualization {
public:
    explicit CrossModuleDevirtualization(LTOContext& context) : context_(context) {}
    
    void Run();
    
    size_t GetDevirtualizedCount() const { return devirtualized_count_; }
    
private:
    LTOContext& context_;
    size_t devirtualized_count_{0};
    
    // Class hierarchy analysis
    struct ClassInfo {
        std::string name;
        std::string base_class;
        std::vector<std::string> derived_classes;
        std::set<std::string> methods;
        bool is_final{false};
    };
    std::map<std::string, ClassInfo> class_hierarchy_;
    
    // Build class hierarchy from type information
    void BuildClassHierarchy();
    
    // Try to devirtualize a call
    bool TryDevirtualize(ir::CallInstruction& call);
    
    // Get the unique implementation of a virtual method
    std::optional<std::string> GetUniqueImplementation(
        const std::string& class_name,
        const std::string& method_name) const;
};

/**
 * Cross-module global value numbering
 * Eliminates redundant computations across module boundaries
 */
class CrossModuleGVN {
public:
    explicit CrossModuleGVN(LTOContext& context) : context_(context) {}
    
    void Run();
    
    size_t GetEliminatedCount() const { return eliminated_count_; }
    
private:
    LTOContext& context_;
    size_t eliminated_count_{0};
    size_t next_value_number_{1};
    
    // Value numbering tables
    struct Expression {
        std::string opcode;
        std::vector<size_t> operand_vns;
        
        bool operator==(const Expression& other) const {
            return opcode == other.opcode && operand_vns == other.operand_vns;
        }
    };
    
    struct ExpressionHash {
        size_t operator()(const Expression& expr) const;
    };
    
    std::map<std::string, size_t> value_numbers_;
    std::unordered_map<Expression, size_t, ExpressionHash> expression_to_vn_;
    std::map<size_t, std::string> vn_to_canonical_;
    
    // Run GVN on a single function
    void RunOnFunction(ir::Function& func);
    
    // Create expression for an instruction
    Expression CreateExpression(const ir::Instruction* inst) const;
    
    // Check if instruction is pure (no side effects)
    bool IsPure(const ir::Instruction* inst) const;
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
        size_t functions_inlined{0};
        size_t functions_removed{0};
        size_t globals_removed{0};
        size_t virtual_calls_devirtualized{0};
        size_t constants_propagated{0};
        size_t redundant_exprs_eliminated{0};
    };
    Statistics GetStatistics() const { return stats_; }
    
private:
    LTOContext& context_;
    Statistics stats_{};
};

// ============ LTO linker integration ============

/**
 * LTO linker
 * Coordinates loading modules, running optimizations, and generating output
 */
class LTOLinker {
public:
    LTOLinker() = default;
    
    // Add an input object file (contains LTO bitcode)
    void AddInputFile(const std::string& filename);
    
    // Set optimization level (0-3)
    void SetOptimizationLevel(int level) { opt_level_ = level; }
    
    // Perform LTO link
    bool Link(const std::string& output_file);
    
    // Configuration options
    struct Config {
        int opt_level = 2;
        bool thin_lto = false;        // Thin LTO mode
        bool emit_bitcode = false;    // Emit optimized bitcode
        bool preserve_symbols = false; // Preserve all symbols (for debugging)
        size_t inline_threshold = InlineCostModel::kDefaultInlineThreshold;
        size_t max_function_size = 10000;
        bool enable_devirtualization = true;
        bool enable_gvn = true;
        bool enable_constant_prop = true;
    };
    void SetConfig(const Config& config) { config_ = config; }
    Config& GetConfig() { return config_; }
    
    // Get link statistics
    struct LinkStats {
        size_t modules_linked{0};
        size_t total_functions{0};
        size_t optimized_functions{0};
        size_t functions_inlined{0};
        size_t functions_removed{0};
        size_t globals_removed{0};
        double optimization_time_ms{0.0};
    };
    LinkStats GetStatistics() const { return stats_; }
    
private:
    std::vector<std::string> input_files_;
    Config config_;
    LinkStats stats_{};
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
 *
 * Process:
 * 1. Generate module summaries in parallel
 * 2. Perform global analysis on summaries
 * 3. Import necessary functions based on analysis
 * 4. Optimize modules in parallel
 */
class ThinLTOCodeGenerator {
public:
    // Add a module
    void AddModule(const std::string& identifier, const std::string& bitcode);
    
    // Run Thin LTO
    bool Run();
    
    // Generate a summary index
    struct FunctionSummary {
        std::string name;
        size_t instruction_count{0};
        size_t block_count{0};
        std::vector<std::string> callees;
        std::vector<std::string> referenced_globals;
        bool is_hot{false};
    };
    
    struct GlobalSummary {
        std::string name;
        size_t size{0};
        bool is_constant{false};
        bool is_weak{false};
    };
    
    struct ModuleSummary {
        std::string module_id;
        std::vector<std::string> defined_symbols;
        std::vector<std::string> referenced_symbols;
        std::map<std::string, size_t> function_sizes;
        std::vector<FunctionSummary> functions;
        std::vector<GlobalSummary> globals;
    };
    std::vector<ModuleSummary> GenerateSummaries() const;
    
    // Import analysis - determine which functions to import into which modules
    std::map<std::string, std::vector<std::string>> ComputeImports() const;
    
    // Optimize in parallel
    void OptimizeInParallel(size_t num_threads);
    
    // Get optimization statistics
    struct ThinLTOStats {
        size_t modules_processed{0};
        size_t functions_imported{0};
        size_t functions_inlined{0};
    };
    ThinLTOStats GetStatistics() const { return stats_; }
    
private:
    std::map<std::string, std::string> modules_;  // id -> bitcode
    std::vector<ModuleSummary> summaries_;
    ThinLTOStats stats_{};
    
    // Build combined index from all summaries
    void BuildCombinedIndex();
    
    // Compute import decisions based on call graph
    void ComputeImportDecisions();
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
    
    // Thin LTO pipeline
    static bool ThinLTO(const std::vector<std::string>& sources,
                       const std::string& output_exe,
                       int opt_level = 2,
                       size_t num_threads = 4);
};

} // namespace polyglot::lto
