/**
 * Link-Time Optimization (LTO) 支持
 * 
 * 在链接时进行跨模块优化
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/cfg.h"

namespace polyglot::lto {

// ============ LTO IR表示 ============

/**
 * LTO使用的中间表示
 * 包含完整的模块信息用于跨模块分析
 */
class LTOModule {
public:
    std::string module_name;
    std::vector<ir::Function> functions;
    std::vector<ir::GlobalVariable> globals;
    
    // 符号导出信息
    std::map<std::string, bool> exported_symbols;  // name -> is_public
    
    // 模块间依赖
    std::vector<std::string> dependencies;
    
    // 序列化为bitcode
    bool SaveBitcode(const std::string& filename) const;
    bool LoadBitcode(const std::string& filename);
};

/**
 * LTO上下文 - 管理所有参与链接的模块
 */
class LTOContext {
public:
    // 添加模块
    void AddModule(std::unique_ptr<LTOModule> module);
    
    // 获取所有模块
    const std::vector<std::unique_ptr<LTOModule>>& GetModules() const {
        return modules_;
    }
    
    // 查找函数（跨模块）
    ir::Function* FindFunction(const std::string& name);
    
    // 查找全局变量
    ir::GlobalVariable* FindGlobal(const std::string& name);
    
    // 构建调用图
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

// ============ LTO优化Passes ============

/**
 * 跨模块内联
 */
class CrossModuleInliner {
public:
    explicit CrossModuleInliner(LTOContext& context) : context_(context) {}
    
    // 执行跨模块内联
    void Run();
    
    // 内联决策
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
 * 全局死代码消除
 */
class GlobalDeadCodeElimination {
public:
    explicit GlobalDeadCodeElimination(LTOContext& context) : context_(context) {}
    
    // 消除未使用的函数和全局变量
    void Run();
    
    // 标记可达的符号
    std::set<std::string> MarkReachableSymbols() const;
    
private:
    LTOContext& context_;
};

/**
 * 常量传播（跨模块）
 */
class InterproceduralConstantPropagation {
public:
    explicit InterproceduralConstantPropagation(LTOContext& context) 
        : context_(context) {}
    
    void Run();
    
private:
    LTOContext& context_;
    
    // 分析常量参数
    std::map<std::string, std::vector<ir::Value>> AnalyzeConstantArgs() const;
};

/**
 * 全局优化器（协调所有LTO优化）
 */
class GlobalOptimizer {
public:
    explicit GlobalOptimizer(LTOContext& context) : context_(context) {}
    
    // 运行完整的LTO优化流程
    void Optimize();
    
    // 单独的优化阶段
    void RunInlining();
    void RunDeadCodeElimination();
    void RunConstantPropagation();
    void RunDevirtualization();
    void RunGlobalValueNumbering();
    
    // 优化统计
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

// ============ LTO链接器集成 ============

/**
 * LTO链接器
 */
class LTOLinker {
public:
    LTOLinker() = default;
    
    // 添加输入对象文件（包含LTO bitcode）
    void AddInputFile(const std::string& filename);
    
    // 设置优化级别
    void SetOptimizationLevel(int level) { opt_level_ = level; }
    
    // 执行LTO链接
    bool Link(const std::string& output_file);
    
    // 配置选项
    struct Config {
        int opt_level = 2;
        bool thin_lto = false;      // Thin LTO模式
        bool emit_bitcode = false;   // 输出优化后的bitcode
        bool preserve_symbols = false; // 保留所有符号（调试用）
        size_t inline_threshold = 225;
        size_t max_function_size = 10000;
    };
    void SetConfig(const Config& config) { config_ = config; }
    
    // 获取链接统计
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
    
    // 加载所有模块
    bool LoadModules(LTOContext& context);
    
    // 优化阶段
    void OptimizeModules(LTOContext& context);
    
    // 代码生成阶段
    bool GenerateCode(const LTOContext& context, const std::string& output);
};

// ============ Thin LTO支持 ============

/**
 * Thin LTO - 可扩展的LTO实现
 * 
 * 相比传统LTO，Thin LTO:
 * - 并行化更好
 * - 内存占用更小
 * - 增量编译友好
 */
class ThinLTOCodeGenerator {
public:
    // 添加模块
    void AddModule(const std::string& identifier, const std::string& bitcode);
    
    // 执行Thin LTO
    bool Run();
    
    // 生成摘要索引
    struct ModuleSummary {
        std::string module_id;
        std::vector<std::string> defined_symbols;
        std::vector<std::string> referenced_symbols;
        std::map<std::string, size_t> function_sizes;
    };
    std::vector<ModuleSummary> GenerateSummaries() const;
    
    // 导入分析
    std::map<std::string, std::vector<std::string>> ComputeImports() const;
    
    // 并行优化
    void OptimizeInParallel(size_t num_threads);
    
private:
    std::map<std::string, std::string> modules_;  // id -> bitcode
    std::vector<ModuleSummary> summaries_;
};

// ============ 工具函数 ============

/**
 * 编译为LTO bitcode
 */
bool CompileToBitcode(const std::string& source_file,
                     const std::string& output_bitcode);

/**
 * 合并bitcode文件
 */
bool MergeBitcode(const std::vector<std::string>& input_files,
                 const std::string& output_file);

/**
 * 从bitcode生成目标代码
 */
bool GenerateObjectFromBitcode(const std::string& bitcode_file,
                              const std::string& output_object);

/**
 * LTO工作流辅助
 */
class LTOWorkflow {
public:
    // Step 1: 编译所有源文件为bitcode
    static bool CompilePhase(const std::vector<std::string>& sources,
                            const std::string& output_dir);
    
    // Step 2: 链接时优化
    static bool LinkPhase(const std::vector<std::string>& bitcodes,
                         const std::string& output_exe);
    
    // 完整流程
    static bool FullLTO(const std::vector<std::string>& sources,
                       const std::string& output_exe,
                       int opt_level = 2);
};

} // namespace polyglot::lto
