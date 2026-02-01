/**
 * Profile-Guided Optimization (PGO) 支持
 * 
 * 收集和使用运行时性能分析数据来优化编译
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

// ============ 性能分析数据结构 ============

/**
 * 基本块执行计数
 */
struct BasicBlockProfile {
    size_t block_id;
    uint64_t execution_count;
    double execution_time_ns;
};

/**
 * 分支预测数据
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
 * 函数调用分析
 */
struct CallSiteProfile {
    size_t call_site_id;
    std::string callee_name;
    uint64_t call_count;
    double avg_call_time_ns;
    
    // 虚函数调用的具体目标
    std::map<std::string, uint64_t> virtual_targets;
};

/**
 * 循环性能数据
 */
struct LoopProfile {
    size_t loop_id;
    uint64_t iteration_count;
    double avg_iterations_per_invocation;
    uint64_t trip_count_min;
    uint64_t trip_count_max;
};

/**
 * 内存访问模式
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
 * 函数性能分析数据
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
    
    // 热点基本块（执行次数前10%）
    std::vector<size_t> GetHotBlocks() const;
    
    // 冷代码块（很少执行）
    std::vector<size_t> GetColdBlocks() const;
    
    // 关键路径分析
    std::vector<size_t> GetCriticalPath() const;
};

/**
 * 完整的性能分析数据
 */
class ProfileData {
public:
    ProfileData() = default;
    
    // 添加函数性能数据
    void AddFunctionProfile(const FunctionProfile& profile);
    
    // 获取函数性能数据
    const FunctionProfile* GetFunctionProfile(const std::string& name) const;
    
    // 合并多个profile数据（用于多次运行）
    void Merge(const ProfileData& other);
    
    // 序列化/反序列化
    bool SaveToFile(const std::string& filename) const;
    bool LoadFromFile(const std::string& filename);
    
    // 统计信息
    size_t GetFunctionCount() const { return functions_.size(); }
    std::vector<std::string> GetHotFunctions(size_t top_n = 10) const;
    
private:
    std::map<std::string, FunctionProfile> functions_;
    std::string profile_version_ = "1.0";
};

// ============ 性能分析数据收集 ============

/**
 * 运行时性能计数器
 */
class RuntimeProfiler {
public:
    static RuntimeProfiler& Instance();
    
    // 记录基本块执行
    void RecordBasicBlock(const std::string& func, size_t block_id);
    
    // 记录分支跳转
    void RecordBranch(const std::string& func, size_t branch_id, bool taken);
    
    // 记录函数调用
    void RecordCall(const std::string& caller, size_t call_site_id, 
                   const std::string& callee);
    
    // 记录循环迭代
    void RecordLoopIteration(const std::string& func, size_t loop_id);
    
    // 获取收集的数据
    ProfileData GetProfileData() const;
    
    // 重置所有计数器
    void Reset();
    
    // 启用/禁用profiling
    void Enable() { enabled_ = true; }
    void Disable() { enabled_ = false; }
    
private:
    RuntimeProfiler() = default;
    bool enabled_ = true;
    
    std::map<std::string, FunctionProfile> profiles_;
};

// ============ PGO优化器 ============

/**
 * 使用profile数据进行优化
 */
class PGOOptimizer {
public:
    explicit PGOOptimizer(const ProfileData& profile) 
        : profile_(profile) {}
    
    // 基于profile的内联决策
    struct InliningDecision {
        std::string caller;
        size_t call_site_id;
        std::string callee;
        bool should_inline;
        std::string reason;
    };
    std::vector<InliningDecision> MakeInliningDecisions() const;
    
    // 基于profile的代码布局优化
    struct CodeLayoutHint {
        std::string function;
        std::vector<size_t> block_order;  // 优化后的基本块顺序
    };
    std::vector<CodeLayoutHint> OptimizeCodeLayout() const;
    
    // 基于profile的循环优化决策
    struct LoopOptimizationHint {
        std::string function;
        size_t loop_id;
        bool should_unroll;
        size_t unroll_factor;
        bool should_vectorize;
    };
    std::vector<LoopOptimizationHint> OptimizeLoops() const;
    
    // 虚函数去虚化
    struct DevirtualizationHint {
        std::string function;
        size_t call_site_id;
        std::string likely_target;
        double probability;
    };
    std::vector<DevirtualizationHint> FindDevirtualizationOpportunities() const;
    
    // 分支预测提示
    struct BranchPredictionHint {
        std::string function;
        size_t branch_id;
        bool likely_taken;
        double confidence;
    };
    std::vector<BranchPredictionHint> GetBranchPredictions() const;
    
private:
    const ProfileData& profile_;
    
    // 内联启发式
    bool ShouldInline(const CallSiteProfile& call_site) const;
    
    // 代码布局启发式
    std::vector<size_t> ComputeOptimalBlockOrder(
        const FunctionProfile& func) const;
};

// ============ 插桩代码生成 ============

/**
 * 为PGO生成插桩代码
 */
class ProfileInstrumentation {
public:
    // 在IR中插入profiling代码
    static void InstrumentFunction(ir::Function& func);
    
    // 生成profiling运行时库调用
    static void GenerateProfilerCalls(ir::Function& func);
    
    // 优化插桩开销
    static void OptimizeInstrumentation(ir::Function& func);
};

// ============ PGO工作流 ============

/**
 * PGO完整工作流程
 * 
 * 使用方式：
 * 1. 构建插桩版本：polyc -fprofile-generate input.cpp -o app.instrumented
 * 2. 运行获取profile：./app.instrumented (生成 default.profdata)
 * 3. 使用profile优化：polyc -fprofile-use=default.profdata input.cpp -o app.optimized
 */
class PGOWorkflow {
public:
    // Step 1: 生成插桩版本
    static bool GenerateInstrumentedBinary(
        const std::string& source_file,
        const std::string& output_file);
    
    // Step 2: 运行并收集profile（由用户执行）
    
    // Step 3: 使用profile优化编译
    static bool CompileWithProfile(
        const std::string& source_file,
        const std::string& profile_file,
        const std::string& output_file);
    
    // 工具：合并多个profile文件
    static bool MergeProfiles(
        const std::vector<std::string>& input_files,
        const std::string& output_file);
};

} // namespace polyglot::pgo
