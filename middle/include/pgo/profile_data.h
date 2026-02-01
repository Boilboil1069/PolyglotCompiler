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
    uint64_t taken_count;
    uint64_t not_taken_count;
    
    double TakenProbability() const {
        uint64_t total = taken_count + not_taken_count;
        return total > 0 ? static_cast<double>(taken_count) / total : 0.5;
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
    
    // Concrete targets of virtual calls
    std::map<std::string, uint64_t> virtual_targets;
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
    uint64_t invocation_count;
    double total_time_ns;
    double avg_time_per_call_ns;
    
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
 * Optimize using profile data
 */
class PGOOptimizer {
public:
    explicit PGOOptimizer(const ProfileData& profile) 
        : profile_(profile) {}
    
    // Profile-guided inlining decisions
    struct InliningDecision {
        std::string caller;
        size_t call_site_id;
        std::string callee;
        bool should_inline;
        std::string reason;
    };
    std::vector<InliningDecision> MakeInliningDecisions() const;
    
    // Profile-guided code layout optimization
    struct CodeLayoutHint {
        std::string function;
        std::vector<size_t> block_order;  // Optimized basic block order
    };
    std::vector<CodeLayoutHint> OptimizeCodeLayout() const;
    
    // Profile-guided loop optimization decisions
    struct LoopOptimizationHint {
        std::string function;
        size_t loop_id;
        bool should_unroll;
        size_t unroll_factor;
        bool should_vectorize;
    };
    std::vector<LoopOptimizationHint> OptimizeLoops() const;
    
    // Devirtualization hints
    struct DevirtualizationHint {
        std::string function;
        size_t call_site_id;
        std::string likely_target;
        double probability;
    };
    std::vector<DevirtualizationHint> FindDevirtualizationOpportunities() const;
    
    // Branch prediction hints
    struct BranchPredictionHint {
        std::string function;
        size_t branch_id;
        bool likely_taken;
        double confidence;
    };
    std::vector<BranchPredictionHint> GetBranchPredictions() const;
    
private:
    const ProfileData& profile_;
    
    // Inlining heuristic
    bool ShouldInline(const CallSiteProfile& call_site) const;
    
    // Code layout heuristic
    std::vector<size_t> ComputeOptimalBlockOrder(
        const FunctionProfile& func) const;
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
