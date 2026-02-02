/**
 * Profile-Guided Optimization (PGO) support
 *
 * Collect and use runtime profiling data to optimize compilation
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace polyglot::ir {
class Function;
}

namespace polyglot::pgo {

// ============ Profiling data structures ============

/**
 * Basic block execution counts
 */
struct BasicBlockProfile {
    size_t block_id;
    uint64_t execution_count;
    double execution_time_ns;
};

/**
 * Branch prediction data
 */
struct BranchProfile {
    size_t branch_id;
    size_t source_block;  // Basic block containing the branch
    uint64_t taken_count;
    uint64_t not_taken_count;
    
    double TakenProbability() const {
        uint64_t total = taken_count + not_taken_count;
        return total > 0 ? static_cast<double>(taken_count) / total : 0.5;
    }
    
    // Check if branch is highly predictable (>90% or <10% taken)
    bool IsHighlyPredictable() const {
        double prob = TakenProbability();
        return prob > 0.9 || prob < 0.1;
    }
    
    // Get total execution count
    uint64_t TotalCount() const {
        return taken_count + not_taken_count;
    }
};

/**
 * Function call analysis
 */
struct CallSiteProfile {
    size_t call_site_id;
    std::string callee_name;
    uint64_t call_count;
    double avg_call_time_ns;
    
    // Estimated callee size (instruction count, 0 if unknown)
    size_t callee_size;
    
    // Whether this is a hot call site
    bool is_hot;
    
    // Concrete targets of virtual calls (target name -> call count)
    std::map<std::string, uint64_t> target_distribution;
    
    // Get the most likely virtual call target with its count
    std::pair<std::string, uint64_t> GetMostLikelyTarget() const {
        if (target_distribution.empty()) {
            return {callee_name, call_count};
        }
        
        std::string best_target;
        uint64_t max_count = 0;
        for (const auto& [target, count] : target_distribution) {
            if (count > max_count) {
                max_count = count;
                best_target = target;
            }
        }
        return {best_target, max_count};
    }
    
    // Check if this is a single-target (monomorphic) call site
    bool has_single_target() const {
        return target_distribution.size() <= 1;
    }
    
    // Get probability of the most likely target
    double GetMostLikelyTargetProbability() const {
        if (target_distribution.empty()) return 1.0;
        
        uint64_t total = 0;
        uint64_t max_count = 0;
        for (const auto& [target, count] : target_distribution) {
            total += count;
            max_count = std::max(max_count, count);
        }
        return total > 0 ? static_cast<double>(max_count) / total : 0.0;
    }
};

/**
 * Loop performance data
 */
struct LoopProfile {
    size_t loop_id;
    uint64_t iteration_count;
    double avg_iterations_per_invocation;
    uint64_t trip_count_min;
    uint64_t trip_count_max;
};

/**
 * Memory access patterns
 */
struct MemoryProfile {
    size_t instruction_id;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t tlb_misses;
    
    double CacheHitRate() const {
        uint64_t total = cache_hits + cache_misses;
        return total > 0 ? static_cast<double>(cache_hits) / total : 0.0;
    }
};

/**
 * Function-level profiling data
 */
class FunctionProfile {
public:
    std::string function_name;
    uint64_t invocation_count{0};
    double total_time_ns{0.0};
    double avg_time_per_call_ns{0.0};
    
    // Estimated function size (instruction count)
    size_t estimated_size{0};
    
    // Whether this function is considered hot
    bool is_hot{false};
    
    std::vector<BasicBlockProfile> basic_blocks;
    std::vector<BranchProfile> branches;
    std::vector<CallSiteProfile> call_sites;
    std::vector<LoopProfile> loops;
    std::vector<MemoryProfile> memory_accesses;
    
    // Hot basic blocks (top 10% by execution count)
    std::vector<size_t> GetHotBlocks() const;
    
    // Cold blocks (rarely executed)
    std::vector<size_t> GetColdBlocks() const;
    
    // Critical path analysis
    std::vector<size_t> GetCriticalPath() const;
    
    // Get branch profile by ID
    const BranchProfile* GetBranchProfile(size_t branch_id) const;
    
    // Get call site profile by ID
    const CallSiteProfile* GetCallSiteProfile(size_t call_site_id) const;
    
    // Check if function is considered hot based on invocation count
    bool IsHotFunction(uint64_t threshold = 1000) const {
        return invocation_count >= threshold;
    }
};

/**
 * Complete profiling dataset
 */
class ProfileData {
public:
    ProfileData() = default;
    
    // Add function profiling data
    void AddFunctionProfile(const FunctionProfile& profile);
    
    // Fetch function profiling data
    const FunctionProfile* GetFunctionProfile(const std::string& name) const;
    
    // Merge multiple profile datasets (for multiple runs)
    void Merge(const ProfileData& other);
    
    // Serialization / deserialization
    bool SaveToFile(const std::string& filename) const;
    bool LoadFromFile(const std::string& filename);
    
    // Statistics
    size_t GetFunctionCount() const { return functions_.size(); }
    std::vector<std::string> GetHotFunctions(size_t top_n = 10) const;
    
    // Iterate over all function profiles
    void ForEachFunction(const std::function<void(const std::string&, const FunctionProfile&)>& callback) const;
    
    // Get all function names
    std::vector<std::string> GetFunctionNames() const;
    
    // Get total invocation count across all functions
    uint64_t GetTotalInvocationCount() const;
    
    // Get average function invocation count
    double GetAverageInvocationCount() const;
    
private:
    std::map<std::string, FunctionProfile> functions_;
    std::string profile_version_ = "1.0";
};

// ============ Profiling data collection ============

/**
 * Runtime profiling counters
 */
class RuntimeProfiler {
public:
    static RuntimeProfiler& Instance();
    
    // Record a basic block execution
    void RecordBasicBlock(const std::string& func, size_t block_id);
    
    // Record a branch outcome
    void RecordBranch(const std::string& func, size_t branch_id, bool taken);
    
    // Record a function call
    void RecordCall(const std::string& caller, size_t call_site_id, 
                   const std::string& callee);
    
    // Record a loop iteration
    void RecordLoopIteration(const std::string& func, size_t loop_id);
    
    // Retrieve collected data
    ProfileData GetProfileData() const;
    
    // Reset all counters
    void Reset();
    
    // Enable / disable profiling
    void Enable() { enabled_ = true; }
    void Disable() { enabled_ = false; }
    
private:
    RuntimeProfiler() = default;
    bool enabled_ = true;
    
    std::map<std::string, FunctionProfile> profiles_;
};

// ============ PGO optimizer ============

/**
 * Profile-Guided Optimization (PGO) optimizer
 *
 * Uses runtime profiling data to make optimization decisions:
 * - Inlining: Inline hot call sites with small callees
 * - Code layout: Reorder basic blocks to improve cache locality
 * - Loop optimization: Unroll/vectorize hot loops
 * - Branch prediction: Hint likely/unlikely branches
 * - Devirtualization: Specialize polymorphic calls
 * - Function specialization: Clone functions for common arguments
 * - Memory prefetching: Insert prefetch hints for strided accesses
 */
class PGOOptimizer {
public:
    // Configuration options for PGO decisions
    struct Config {
        // Inlining thresholds
        uint64_t hot_call_threshold = 1000;       // Minimum calls to be considered hot
        size_t max_inline_size = 100;             // Maximum instruction count for inlining
        size_t always_inline_size = 10;           // Always inline functions smaller than this
        double inline_benefit_ratio = 1.5;        // Call count / size ratio threshold
        double size_growth_factor = 0.1;          // How much we penalize code size growth
        
        // Branch prediction thresholds
        double branch_prediction_threshold = 0.9; // Probability threshold for hints
        uint64_t min_branch_samples = 100;        // Minimum samples for reliable prediction
        
        // Loop optimization thresholds
        uint64_t loop_unroll_threshold = 8;       // Min iterations for unroll consideration
        uint64_t vectorization_threshold = 4;     // Min iterations for vectorization
        
        // Devirtualization thresholds
        double devirt_threshold = 0.8;            // Target probability for devirtualization
        uint64_t devirt_min_samples = 50;         // Minimum samples for devirtualization
        
        // Code layout thresholds
        double hot_block_threshold = 0.1;         // Top 10% blocks are hot
        double cold_block_threshold = 0.01;       // Bottom 1% blocks are cold
        double cold_function_threshold = 0.1;     // Functions with <10% avg calls are cold
    };
    
    explicit PGOOptimizer(const ProfileData& profile);
    PGOOptimizer(const ProfileData& profile, const Config& config);
    
    // Set configuration
    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }
    
    // ============ Inlining decisions ============
    
    struct InliningDecision {
        std::string caller;           // Caller function name
        size_t call_site_id;          // Call site ID within caller
        std::string callee;           // Callee function name
        bool should_inline;           // Whether to inline
        int priority;                 // Priority (0-3, higher = more important)
        double expected_benefit;      // Estimated performance benefit (ns)
        std::string reason;           // Human-readable explanation
    };
    
    std::vector<InliningDecision> MakeInliningDecisions() const;
    
    // ============ Code layout optimization ============
    
    struct CodeLayoutHint {
        std::string function;                 // Function name
        std::vector<size_t> block_order;      // Optimized basic block order
        std::vector<size_t> hot_blocks;       // Blocks to keep in hot section
        std::vector<size_t> cold_blocks;      // Blocks to move to cold section
        double expected_benefit;              // Estimated cache improvement (%)
        std::string reason;                   // Human-readable explanation
    };
    
    std::vector<CodeLayoutHint> OptimizeCodeLayout() const;
    
    // ============ Loop optimization ============
    
    struct LoopOptimizationHint {
        std::string function;             // Function containing the loop
        size_t loop_header_block;         // Loop header basic block ID
        bool should_unroll;               // Whether to unroll the loop
        size_t unroll_factor;             // Suggested unroll factor
        bool should_vectorize;            // Whether to vectorize
        double expected_benefit;          // Estimated benefit (ns)
        std::string reason;               // Human-readable explanation
    };
    
    std::vector<LoopOptimizationHint> OptimizeLoops() const;
    
    // ============ Branch prediction ============
    
    struct BranchPredictionHint {
        std::string function;             // Function containing the branch
        size_t branch_id;                 // Branch ID
        size_t block_id;                  // Basic block containing the branch
        bool likely_taken;                // Whether branch is likely taken
        double confidence;                // Confidence in prediction (0.0 - 1.0)
        double taken_probability;         // Actual taken probability
        std::string reason;               // Human-readable explanation
    };
    
    std::vector<BranchPredictionHint> GetBranchPredictions() const;
    
    // ============ Devirtualization ============
    
    struct DevirtualizationHint {
        std::string function;             // Function containing the call
        size_t call_site_id;              // Virtual call site ID
        std::string likely_target;        // Most likely concrete target
        double confidence;                // Confidence in prediction (0.0 - 1.0)
        uint64_t call_count;              // Total calls to this site
        bool should_specialize;           // Whether to fully specialize (>95%)
        std::string reason;               // Human-readable explanation
    };
    
    std::vector<DevirtualizationHint> FindDevirtualizationOpportunities() const;
    
    // ============ Function specialization ============
    
    struct SpecializationHint {
        std::string function;             // Function to specialize
        size_t call_site_id;              // Call site triggering specialization
        std::string caller;               // Caller function
        uint64_t call_count;              // Call count at this site
        bool should_specialize;           // Whether specialization is worthwhile
        std::vector<std::pair<size_t, int64_t>> constant_args;  // (arg_index, value)
        double expected_benefit;          // Estimated benefit (ns)
        std::string reason;               // Human-readable explanation
    };
    
    std::vector<SpecializationHint> FindSpecializationOpportunities() const;
    
    // ============ Memory prefetching ============
    
    struct PrefetchHint {
        std::string function;             // Function containing access
        size_t block_id;                  // Basic block with access
        size_t prefetch_distance;         // Suggested prefetch distance (iterations)
        uint64_t target_address;          // Base address to prefetch (if known)
        int locality;                     // Cache locality hint (0-3)
        double expected_benefit;          // Estimated benefit (ns)
        std::string reason;               // Human-readable explanation
    };
    
    std::vector<PrefetchHint> GetPrefetchHints() const;
    
    // ============ Analysis helpers ============
    
    // Get hottest functions by time spent
    std::vector<std::string> GetHotFunctions(size_t top_n = 10) const;
    
    // Get coldest functions (candidates for outline/remove)
    std::vector<std::string> GetColdFunctions() const;
    
    // ============ Report generation ============
    
    struct OptimizationReport {
        size_t total_functions;
        size_t hot_functions;
        size_t cold_functions;
        size_t inlining_candidates;
        size_t predictable_branches;
        size_t devirt_candidates;
        double estimated_speedup_percent;
        
        std::vector<InliningDecision> inlining_decisions;
        std::vector<BranchPredictionHint> branch_predictions;
        std::vector<CodeLayoutHint> layout_hints;
        std::vector<LoopOptimizationHint> loop_hints;
        std::vector<DevirtualizationHint> devirt_hints;
        std::vector<SpecializationHint> specialization_hints;
        std::vector<PrefetchHint> prefetch_hints;
    };
    
    // Generate comprehensive optimization report
    OptimizationReport GenerateReport() const;
    
    // Export report to JSON file
    bool ExportReportToJson(const std::string& filename) const;
    
private:
    const ProfileData& profile_;
    Config config_;
    
    // Internal helpers
    bool ShouldInline(const CallSiteProfile& call_site) const;
    
    InliningDecision MakeInliningDecision(
        const std::string& caller,
        const CallSiteProfile& call_site,
        const FunctionProfile& caller_profile) const;
    
    BranchPredictionHint MakeBranchPrediction(
        const std::string& function,
        const BranchProfile& branch) const;
};

// ============ Instrumentation code generation ============

/**
 * Generate instrumentation code for PGO
 */
class ProfileInstrumentation {
public:
    // Insert profiling code into the IR
    static void InstrumentFunction(ir::Function& func);
    
    // Generate calls to the profiling runtime library
    static void GenerateProfilerCalls(ir::Function& func);
    
    // Reduce instrumentation overhead
    static void OptimizeInstrumentation(ir::Function& func);
};

// ============ PGO workflow ============

/**
 * End-to-end PGO workflow
 *
 * Usage:
 * 1. Build instrumented binary: polyc -fprofile-generate input.cpp -o app.instrumented
 * 2. Run to gather profile: ./app.instrumented (produces default.profdata)
 * 3. Use profile to optimize: polyc -fprofile-use=default.profdata input.cpp -o app.optimized
 */
class PGOWorkflow {
public:
    // Step 1: Generate instrumented binary
    static bool GenerateInstrumentedBinary(
        const std::string& source_file,
        const std::string& output_file);
    
    // Step 2: Run and collect the profile (user executes)
    
    // Step 3: Compile using the collected profile
    static bool CompileWithProfile(
        const std::string& source_file,
        const std::string& profile_file,
        const std::string& output_file);
    
    // Utility: merge multiple profile files
    static bool MergeProfiles(
        const std::vector<std::string>& input_files,
        const std::string& output_file);
};

} // namespace polyglot::pgo
