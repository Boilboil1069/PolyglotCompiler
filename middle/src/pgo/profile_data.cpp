/**
 * PGO profile data implementation
 */

#include "middle/include/pgo/profile_data.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_set>

using json = nlohmann::json;

namespace polyglot::pgo {

// ============ FunctionProfile implementation ============

std::vector<size_t> FunctionProfile::GetHotBlocks() const {
    if (basic_blocks.empty()) return {};
    
    // Find the hottest 10% of basic blocks by execution count
    std::vector<std::pair<size_t, uint64_t>> blocks;
    for (const auto& bb : basic_blocks) {
        blocks.push_back({bb.block_id, bb.execution_count});
    }
    
    std::sort(blocks.begin(), blocks.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    size_t hot_count = std::max<size_t>(1, blocks.size() / 10);
    std::vector<size_t> hot_blocks;
    for (size_t i = 0; i < hot_count; ++i) {
        hot_blocks.push_back(blocks[i].first);
    }
    
    return hot_blocks;
}

std::vector<size_t> FunctionProfile::GetColdBlocks() const {
    if (basic_blocks.empty()) return {};
    
    // Execution count < 10% of the average
    uint64_t total = 0;
    for (const auto& bb : basic_blocks) {
        total += bb.execution_count;
    }
    uint64_t avg = total / basic_blocks.size();
    uint64_t threshold = avg / 10;
    
    std::vector<size_t> cold_blocks;
    for (const auto& bb : basic_blocks) {
        if (bb.execution_count < threshold) {
            cold_blocks.push_back(bb.block_id);
        }
    }
    
    return cold_blocks;
}

std::vector<size_t> FunctionProfile::GetCriticalPath() const {
    // Simplified: return the basic blocks with the largest execution time
    std::vector<std::pair<size_t, double>> blocks;
    for (const auto& bb : basic_blocks) {
        blocks.push_back({bb.block_id, bb.execution_time_ns});
    }
    
    std::sort(blocks.begin(), blocks.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::vector<size_t> path;
    for (size_t i = 0; i < std::min<size_t>(5, blocks.size()); ++i) {
        path.push_back(blocks[i].first);
    }
    
    return path;
}

const BranchProfile* FunctionProfile::GetBranchProfile(size_t branch_id) const {
    for (const auto& br : branches) {
        if (br.branch_id == branch_id) {
            return &br;
        }
    }
    return nullptr;
}

const CallSiteProfile* FunctionProfile::GetCallSiteProfile(size_t call_site_id) const {
    for (const auto& cs : call_sites) {
        if (cs.call_site_id == call_site_id) {
            return &cs;
        }
    }
    return nullptr;
}

// ============ ProfileData implementation ============

void ProfileData::AddFunctionProfile(const FunctionProfile& profile) {
    functions_[profile.function_name] = profile;
}

const FunctionProfile* ProfileData::GetFunctionProfile(
    const std::string& name) const {
    auto it = functions_.find(name);
    return it != functions_.end() ? &it->second : nullptr;
}

void ProfileData::ForEachFunction(
    const std::function<void(const std::string&, const FunctionProfile&)>& callback) const {
    for (const auto& [name, profile] : functions_) {
        callback(name, profile);
    }
}

std::vector<std::string> ProfileData::GetFunctionNames() const {
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (const auto& [name, _] : functions_) {
        names.push_back(name);
    }
    return names;
}

uint64_t ProfileData::GetTotalInvocationCount() const {
    uint64_t total = 0;
    for (const auto& [_, profile] : functions_) {
        total += profile.invocation_count;
    }
    return total;
}

double ProfileData::GetAverageInvocationCount() const {
    if (functions_.empty()) return 0.0;
    return static_cast<double>(GetTotalInvocationCount()) / functions_.size();
}

void ProfileData::Merge(const ProfileData& other) {
    for (const auto& [name, other_profile] : other.functions_) {
        auto it = functions_.find(name);
        if (it == functions_.end()) {
            functions_[name] = other_profile;
        } else {
            // Merge execution counts
            auto& profile = it->second;
            profile.invocation_count += other_profile.invocation_count;
            profile.total_time_ns += other_profile.total_time_ns;
            
            // Merge basic block data
            for (const auto& other_bb : other_profile.basic_blocks) {
                bool found = false;
                for (auto& bb : profile.basic_blocks) {
                    if (bb.block_id == other_bb.block_id) {
                        bb.execution_count += other_bb.execution_count;
                        bb.execution_time_ns += other_bb.execution_time_ns;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    profile.basic_blocks.push_back(other_bb);
                }
            }
            
            // Merge branch data
            for (const auto& other_br : other_profile.branches) {
                bool found = false;
                for (auto& br : profile.branches) {
                    if (br.branch_id == other_br.branch_id) {
                        br.taken_count += other_br.taken_count;
                        br.not_taken_count += other_br.not_taken_count;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    profile.branches.push_back(other_br);
                }
            }
        }
    }
}

bool ProfileData::SaveToFile(const std::string& filename) const {
    json j;
    j["version"] = profile_version_;
    
    json functions_json = json::array();
    for (const auto& [name, profile] : functions_) {
        json func_json;
        func_json["name"] = name;
        func_json["invocations"] = profile.invocation_count;
        func_json["total_time_ns"] = profile.total_time_ns;
        
        json blocks_json = json::array();
        for (const auto& bb : profile.basic_blocks) {
            json bb_json;
            bb_json["id"] = bb.block_id;
            bb_json["count"] = bb.execution_count;
            bb_json["time_ns"] = bb.execution_time_ns;
            blocks_json.push_back(bb_json);
        }
        func_json["basic_blocks"] = blocks_json;
        
        json branches_json = json::array();
        for (const auto& br : profile.branches) {
            json br_json;
            br_json["id"] = br.branch_id;
            br_json["taken"] = br.taken_count;
            br_json["not_taken"] = br.not_taken_count;
            branches_json.push_back(br_json);
        }
        func_json["branches"] = branches_json;
        
        functions_json.push_back(func_json);
    }
    j["functions"] = functions_json;
    
    std::ofstream out(filename);
    if (!out) return false;
    
    out << j.dump(4);
    return true;
}

bool ProfileData::LoadFromFile(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) return false;
    
    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    
    if (!j.contains("version") || !j.contains("functions")) {
        return false;
    }
    
    for (const auto& func_json : j["functions"]) {
        FunctionProfile profile;
        profile.function_name = func_json["name"];
        profile.invocation_count = func_json["invocations"];
        profile.total_time_ns = func_json["total_time_ns"];
        
        if (func_json.contains("basic_blocks")) {
            for (const auto& bb_json : func_json["basic_blocks"]) {
                BasicBlockProfile bb;
                bb.block_id = bb_json["id"];
                bb.execution_count = bb_json["count"];
                bb.execution_time_ns = bb_json["time_ns"];
                profile.basic_blocks.push_back(bb);
            }
        }
        
        if (func_json.contains("branches")) {
            for (const auto& br_json : func_json["branches"]) {
                BranchProfile br;
                br.branch_id = br_json["id"];
                br.taken_count = br_json["taken"];
                br.not_taken_count = br_json["not_taken"];
                profile.branches.push_back(br);
            }
        }
        
        AddFunctionProfile(profile);
    }
    
    return true;
}

std::vector<std::string> ProfileData::GetHotFunctions(size_t top_n) const {
    std::vector<std::pair<std::string, double>> funcs;
    for (const auto& [name, profile] : functions_) {
        funcs.push_back({name, profile.total_time_ns});
    }
    
    std::sort(funcs.begin(), funcs.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::vector<std::string> hot_funcs;
    for (size_t i = 0; i < std::min(top_n, funcs.size()); ++i) {
        hot_funcs.push_back(funcs[i].first);
    }
    
    return hot_funcs;
}

// ============ RuntimeProfiler implementation ============

RuntimeProfiler& RuntimeProfiler::Instance() {
    static RuntimeProfiler instance;
    return instance;
}

void RuntimeProfiler::RecordBasicBlock(const std::string& func, size_t block_id) {
    if (!enabled_) return;
    
    auto& profile = profiles_[func];
    profile.function_name = func;  // Ensure function name is set
    
    for (auto& bb : profile.basic_blocks) {
        if (bb.block_id == block_id) {
            bb.execution_count++;
            return;
        }
    }
    
    BasicBlockProfile bb;
    bb.block_id = block_id;
    bb.execution_count = 1;
    profile.basic_blocks.push_back(bb);
}

void RuntimeProfiler::RecordBranch(const std::string& func, 
                                  size_t branch_id, bool taken) {
    if (!enabled_) return;
    
    auto& profile = profiles_[func];
    profile.function_name = func;  // Ensure function name is set
    
    for (auto& br : profile.branches) {
        if (br.branch_id == branch_id) {
            if (taken) {
                br.taken_count++;
            } else {
                br.not_taken_count++;
            }
            return;
        }
    }
    
    BranchProfile br;
    br.branch_id = branch_id;
    br.source_block = 0;  // Default source block
    if (taken) {
        br.taken_count = 1;
        br.not_taken_count = 0;
    } else {
        br.taken_count = 0;
        br.not_taken_count = 1;
    }
    profile.branches.push_back(br);
}

ProfileData RuntimeProfiler::GetProfileData() const {
    ProfileData data;
    for (const auto& [name, profile] : profiles_) {
        data.AddFunctionProfile(profile);
    }
    return data;
}

void RuntimeProfiler::Reset() {
    profiles_.clear();
}

// ============ PGOOptimizer implementation ============

PGOOptimizer::PGOOptimizer(const ProfileData& profile)
    : profile_(profile), config_() {}

PGOOptimizer::PGOOptimizer(const ProfileData& profile, const Config& config)
    : profile_(profile), config_(config) {}

std::vector<PGOOptimizer::InliningDecision> 
PGOOptimizer::MakeInliningDecisions() const {
    std::vector<InliningDecision> decisions;
    
    // Iterate over all function profiles using the public ForEachFunction API
    profile_.ForEachFunction([&](const std::string& func_name, 
                                  const FunctionProfile& profile) {
        // Process each call site in this function
        for (const auto& call_site : profile.call_sites) {
            InliningDecision decision = MakeInliningDecision(
                func_name, call_site, profile);
            decisions.push_back(decision);
        }
    });
    
    // Sort decisions by expected benefit (priority * call_count)
    std::sort(decisions.begin(), decisions.end(),
        [](const InliningDecision& a, const InliningDecision& b) {
            return (a.priority * a.expected_benefit) > (b.priority * b.expected_benefit);
        });
    
    return decisions;
}

PGOOptimizer::InliningDecision PGOOptimizer::MakeInliningDecision(
    const std::string& caller,
    const CallSiteProfile& call_site,
    const FunctionProfile& caller_profile) const {
    
    InliningDecision decision;
    decision.caller = caller;
    decision.call_site_id = call_site.call_site_id;
    decision.callee = call_site.callee_name;
    decision.should_inline = false;
    decision.priority = 0;
    decision.expected_benefit = 0.0;
    decision.reason = "";
    
    // Check if the call site is hot
    bool is_hot_call = call_site.call_count >= config_.hot_call_threshold;
    
    // Check callee size (if available)
    const FunctionProfile* callee_profile = profile_.GetFunctionProfile(
        call_site.callee_name);
    size_t callee_size = callee_profile ? callee_profile->estimated_size : 
                         call_site.callee_size;
    
    // Calculate the expected benefit from inlining
    // Benefit = (call_count * estimated_call_overhead) - size_increase_cost
    double call_overhead_ns = 5.0;  // Estimated overhead per call in nanoseconds
    double expected_savings = call_site.call_count * call_overhead_ns;
    double size_cost = static_cast<double>(callee_size) * config_.size_growth_factor;
    double net_benefit = expected_savings - size_cost;
    
    decision.expected_benefit = net_benefit;
    
    // Decision heuristics
    if (is_hot_call && callee_size <= config_.max_inline_size) {
        // Hot and small: definitely inline
        decision.should_inline = true;
        decision.priority = 3;
        decision.reason = "Hot call site (" + std::to_string(call_site.call_count) +
                         " calls) with small callee (" + std::to_string(callee_size) +
                         " instructions)";
    }
    else if (is_hot_call && callee_size <= config_.max_inline_size * 2) {
        // Hot but medium size: inline if benefit is positive
        if (net_benefit > 0) {
            decision.should_inline = true;
            decision.priority = 2;
            decision.reason = "Hot call site with medium callee, positive benefit (" +
                             std::to_string(static_cast<int>(net_benefit)) + " ns)";
        } else {
            decision.reason = "Hot call site but callee too large (size=" +
                             std::to_string(callee_size) + ")";
        }
    }
    else if (callee_size <= config_.always_inline_size) {
        // Very small function: always inline regardless of hotness
        decision.should_inline = true;
        decision.priority = 1;
        decision.reason = "Very small callee (" + std::to_string(callee_size) +
                         " instructions), always inline";
    }
    else if (call_site.is_hot && call_site.has_single_target()) {
        // Marked as hot with single target (good for devirtualization)
        decision.should_inline = true;
        decision.priority = 2;
        decision.reason = "Hot virtual call with single target, inline for devirtualization";
    }
    else {
        // Cold or too large
        decision.reason = "Cold call site (" + std::to_string(call_site.call_count) +
                         " calls) or large callee (" + std::to_string(callee_size) +
                         " instructions)";
    }
    
    return decision;
}

bool PGOOptimizer::ShouldInline(const CallSiteProfile& call_site) const {
    // Quick check using the hot call threshold
    if (call_site.call_count < config_.hot_call_threshold) {
        return false;
    }
    
    // Check if callee is small enough
    if (call_site.callee_size > config_.max_inline_size) {
        return false;
    }
    
    return true;
}

std::vector<PGOOptimizer::BranchPredictionHint>
PGOOptimizer::GetBranchPredictions() const {
    std::vector<BranchPredictionHint> hints;
    
    // Iterate over all function profiles using the public ForEachFunction API
    profile_.ForEachFunction([&](const std::string& func_name,
                                  const FunctionProfile& profile) {
        // Process each branch in this function
        for (const auto& branch : profile.branches) {
            BranchPredictionHint hint = MakeBranchPrediction(func_name, branch);
            
            // Only include hints where probability is highly skewed
            // (i.e., confidence >= threshold means prediction is reliable)
            // Confidence of 0.9 means probability is either <= 5% or >= 95%
            if (hint.confidence >= config_.branch_prediction_threshold) {
                hints.push_back(hint);
            }
        }
    });
    
    // Sort by confidence (highest first)
    std::sort(hints.begin(), hints.end(),
        [](const BranchPredictionHint& a, const BranchPredictionHint& b) {
            return a.confidence > b.confidence;
        });
    
    return hints;
}

PGOOptimizer::BranchPredictionHint PGOOptimizer::MakeBranchPrediction(
    const std::string& function,
    const BranchProfile& branch) const {
    
    BranchPredictionHint hint;
    hint.function = function;
    hint.branch_id = branch.branch_id;
    hint.block_id = branch.source_block;
    
    double prob = branch.TakenProbability();
    hint.taken_probability = prob;
    hint.likely_taken = prob >= 0.5;
    
    // Confidence is how far we are from 50% (uncertain)
    // 0.5 -> 0.0 confidence, 0.0 or 1.0 -> 1.0 confidence
    hint.confidence = std::abs(prob - 0.5) * 2.0;
    
    // Generate reason string
    uint64_t total = branch.TotalCount();
    if (total == 0) {
        hint.reason = "No branch data available";
        hint.confidence = 0.0;
    } else if (prob >= 0.95) {
        hint.reason = "Highly likely taken (" + 
                     std::to_string(static_cast<int>(prob * 100)) + "% taken, " +
                     std::to_string(total) + " samples)";
    } else if (prob <= 0.05) {
        hint.reason = "Highly unlikely taken (" +
                     std::to_string(static_cast<int>(prob * 100)) + "% taken, " +
                     std::to_string(total) + " samples)";
    } else if (prob >= 0.8) {
        hint.reason = "Likely taken (" +
                     std::to_string(static_cast<int>(prob * 100)) + "% taken)";
    } else if (prob <= 0.2) {
        hint.reason = "Unlikely taken (" +
                     std::to_string(static_cast<int>(prob * 100)) + "% taken)";
    } else {
        hint.reason = "Uncertain prediction (" +
                     std::to_string(static_cast<int>(prob * 100)) + "% taken)";
    }
    
    return hint;
}

std::vector<PGOOptimizer::CodeLayoutHint>
PGOOptimizer::OptimizeCodeLayout() const {
    std::vector<CodeLayoutHint> hints;
    
    profile_.ForEachFunction([&](const std::string& func_name,
                                  const FunctionProfile& profile) {
        CodeLayoutHint hint;
        hint.function = func_name;
        
        // Determine optimal basic block order based on execution frequency
        std::vector<std::pair<size_t, uint64_t>> block_counts;
        for (const auto& bb : profile.basic_blocks) {
            block_counts.push_back({bb.block_id, bb.execution_count});
        }
        
        // Sort by execution count (descending) to group hot blocks together
        std::sort(block_counts.begin(), block_counts.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        for (const auto& [block_id, _] : block_counts) {
            hint.block_order.push_back(block_id);
        }
        
        // Identify hot and cold blocks
        hint.hot_blocks = profile.GetHotBlocks();
        hint.cold_blocks = profile.GetColdBlocks();
        
        // Calculate benefit (reduction in branch mispredictions)
        // Simplified: estimate based on hot block locality
        size_t hot_count = hint.hot_blocks.size();
        size_t total_blocks = profile.basic_blocks.size();
        if (total_blocks > 0) {
            hint.expected_benefit = static_cast<double>(hot_count) / total_blocks * 100.0;
        }
        
        hint.reason = "Reorder blocks to improve cache locality";
        hints.push_back(hint);
    });
    
    return hints;
}

std::vector<PGOOptimizer::LoopOptimizationHint>
PGOOptimizer::OptimizeLoops() const {
    std::vector<LoopOptimizationHint> hints;
    
    profile_.ForEachFunction([&](const std::string& func_name,
                                  const FunctionProfile& profile) {
        // Analyze loops based on basic block execution patterns
        // Loop headers typically have high execution counts
        for (const auto& bb : profile.basic_blocks) {
            // Heuristic: blocks with execution count >> invocation count are likely loops
            if (profile.invocation_count > 0 &&
                bb.execution_count > profile.invocation_count * 10) {
                
                LoopOptimizationHint hint;
                hint.function = func_name;
                hint.loop_header_block = bb.block_id;
                
                // Estimate iteration count
                uint64_t estimated_iterations = bb.execution_count / profile.invocation_count;
                
                // Suggest unrolling for small iteration counts
                if (estimated_iterations >= config_.loop_unroll_threshold &&
                    estimated_iterations <= 16) {
                    hint.should_unroll = true;
                    hint.unroll_factor = static_cast<size_t>(
                        std::min<uint64_t>(estimated_iterations, 8));
                    hint.reason = "Loop executes ~" + 
                                 std::to_string(estimated_iterations) +
                                 " iterations on average";
                }
                
                // Suggest vectorization for larger iteration counts
                if (estimated_iterations > 16) {
                    hint.should_vectorize = true;
                    hint.reason = "Loop executes ~" +
                                 std::to_string(estimated_iterations) +
                                 " iterations, good vectorization candidate";
                }
                
                // Calculate expected benefit
                double loop_time = bb.execution_time_ns;
                if (hint.should_unroll) {
                    // Unrolling typically saves 10-20% of loop overhead
                    hint.expected_benefit = loop_time * 0.15;
                } else if (hint.should_vectorize) {
                    // Vectorization can give 2-4x speedup
                    hint.expected_benefit = loop_time * 0.5;
                }
                
                if (hint.should_unroll || hint.should_vectorize) {
                    hints.push_back(hint);
                }
            }
        }
    });
    
    // Sort by expected benefit
    std::sort(hints.begin(), hints.end(),
        [](const LoopOptimizationHint& a, const LoopOptimizationHint& b) {
            return a.expected_benefit > b.expected_benefit;
        });
    
    return hints;
}

std::vector<PGOOptimizer::DevirtualizationHint>
PGOOptimizer::FindDevirtualizationOpportunities() const {
    std::vector<DevirtualizationHint> hints;
    
    profile_.ForEachFunction([&](const std::string& func_name,
                                  const FunctionProfile& profile) {
        for (const auto& call_site : profile.call_sites) {
            // Look for polymorphic call sites with dominant target
            if (!call_site.target_distribution.empty()) {
                // Find the most common target
                auto most_likely = call_site.GetMostLikelyTarget();
                
                if (!most_likely.first.empty()) {
                    uint64_t total_calls = 0;
                    for (const auto& [_, count] : call_site.target_distribution) {
                        total_calls += count;
                    }
                    
                    if (total_calls > 0) {
                        double dominant_ratio = static_cast<double>(most_likely.second) / 
                                               total_calls;
                        
                        // If one target handles > 80% of calls, suggest devirtualization
                        if (dominant_ratio >= config_.devirt_threshold) {
                            DevirtualizationHint hint;
                            hint.function = func_name;
                            hint.call_site_id = call_site.call_site_id;
                            hint.likely_target = most_likely.first;
                            hint.confidence = dominant_ratio;
                            hint.call_count = total_calls;
                            hint.should_specialize = dominant_ratio >= 0.95;
                            hint.reason = "Virtual call has dominant target (" +
                                         std::to_string(static_cast<int>(dominant_ratio * 100)) +
                                         "% calls to " + most_likely.first + ")";
                            hints.push_back(hint);
                        }
                    }
                }
            }
        }
    });
    
    // Sort by confidence and call count
    std::sort(hints.begin(), hints.end(),
        [](const DevirtualizationHint& a, const DevirtualizationHint& b) {
            return (a.confidence * a.call_count) > (b.confidence * b.call_count);
        });
    
    return hints;
}

std::vector<PGOOptimizer::SpecializationHint>
PGOOptimizer::FindSpecializationOpportunities() const {
    std::vector<SpecializationHint> hints;
    
    profile_.ForEachFunction([&](const std::string& func_name,
                                  const FunctionProfile& profile) {
        // Look for functions that are called with constant arguments
        for (const auto& call_site : profile.call_sites) {
            // Check if this call site has consistent argument patterns
            // For now, we use the target distribution as a proxy for specialization
            if (call_site.is_hot && call_site.call_count >= config_.hot_call_threshold) {
                SpecializationHint hint;
                hint.function = call_site.callee_name;
                hint.call_site_id = call_site.call_site_id;
                hint.caller = func_name;
                hint.call_count = call_site.call_count;
                
                // If we have value profiling data, use it
                // For now, mark hot call sites as specialization candidates
                if (call_site.has_single_target()) {
                    hint.should_specialize = true;
                    hint.constant_args = {}; // Would be populated by value profiling
                    hint.reason = "Hot call site with consistent target";
                    hint.expected_benefit = static_cast<double>(call_site.call_count) * 2.0;
                    hints.push_back(hint);
                }
            }
        }
    });
    
    // Sort by expected benefit
    std::sort(hints.begin(), hints.end(),
        [](const SpecializationHint& a, const SpecializationHint& b) {
            return a.expected_benefit > b.expected_benefit;
        });
    
    return hints;
}

std::vector<PGOOptimizer::PrefetchHint>
PGOOptimizer::GetPrefetchHints() const {
    std::vector<PrefetchHint> hints;
    
    profile_.ForEachFunction([&](const std::string& func_name,
                                  const FunctionProfile& profile) {
        // Analyze memory access patterns from basic block execution times
        // High execution time relative to count suggests memory stalls
        for (size_t i = 0; i < profile.basic_blocks.size(); ++i) {
            const auto& bb = profile.basic_blocks[i];
            
            if (bb.execution_count > 0) {
                double avg_time = bb.execution_time_ns / bb.execution_count;
                
                // If average time per execution is high, suggest prefetching
                // Threshold: > 100ns per execution suggests memory latency
                if (avg_time > 100.0 && bb.execution_count >= 100) {
                    PrefetchHint hint;
                    hint.function = func_name;
                    hint.block_id = bb.block_id;
                    
                    // Suggest prefetching for the next likely block
                    if (i + 1 < profile.basic_blocks.size()) {
                        hint.prefetch_distance = 1;
                        hint.target_address = 0;  // Would be filled by analysis
                        hint.locality = 3;  // Temporal locality hint (L1 cache)
                        hint.expected_benefit = avg_time * 0.3;  // ~30% latency hiding
                        hint.reason = "High average execution time (" +
                                     std::to_string(static_cast<int>(avg_time)) +
                                     " ns) suggests memory stalls";
                        hints.push_back(hint);
                    }
                }
            }
        }
    });
    
    // Sort by expected benefit
    std::sort(hints.begin(), hints.end(),
        [](const PrefetchHint& a, const PrefetchHint& b) {
            return a.expected_benefit > b.expected_benefit;
        });
    
    return hints;
}

std::vector<std::string> PGOOptimizer::GetHotFunctions(size_t top_n) const {
    return profile_.GetHotFunctions(top_n);
}

std::vector<std::string> PGOOptimizer::GetColdFunctions() const {
    std::vector<std::string> cold_funcs;
    double avg_invocations = profile_.GetAverageInvocationCount();
    double threshold = avg_invocations * config_.cold_function_threshold;
    
    profile_.ForEachFunction([&](const std::string& func_name,
                                  const FunctionProfile& profile) {
        if (profile.invocation_count < threshold) {
            cold_funcs.push_back(func_name);
        }
    });
    
    return cold_funcs;
}

PGOOptimizer::OptimizationReport PGOOptimizer::GenerateReport() const {
    OptimizationReport report;
    
    // Gather all optimization opportunities
    report.inlining_decisions = MakeInliningDecisions();
    report.branch_predictions = GetBranchPredictions();
    report.layout_hints = OptimizeCodeLayout();
    report.loop_hints = OptimizeLoops();
    report.devirt_hints = FindDevirtualizationOpportunities();
    report.specialization_hints = FindSpecializationOpportunities();
    report.prefetch_hints = GetPrefetchHints();
    
    // Calculate totals
    report.total_functions = profile_.GetFunctionCount();
    report.hot_functions = GetHotFunctions(10).size();
    report.cold_functions = GetColdFunctions().size();
    
    // Count recommended optimizations
    report.inlining_candidates = 0;
    for (const auto& d : report.inlining_decisions) {
        if (d.should_inline) report.inlining_candidates++;
    }
    
    report.predictable_branches = report.branch_predictions.size();
    report.devirt_candidates = report.devirt_hints.size();
    
    // Calculate estimated speedup (simplified)
    double total_benefit = 0.0;
    for (const auto& d : report.inlining_decisions) {
        if (d.should_inline) total_benefit += d.expected_benefit;
    }
    for (const auto& h : report.loop_hints) {
        total_benefit += h.expected_benefit;
    }
    for (const auto& h : report.prefetch_hints) {
        total_benefit += h.expected_benefit;
    }
    
    report.estimated_speedup_percent = total_benefit / 
        std::max<double>(1.0, profile_.GetTotalInvocationCount()) * 100.0;
    
    return report;
}

bool PGOOptimizer::ExportReportToJson(const std::string& filename) const {
    OptimizationReport report = GenerateReport();
    
    json j;
    j["version"] = "1.0";
    j["summary"] = {
        {"total_functions", report.total_functions},
        {"hot_functions", report.hot_functions},
        {"cold_functions", report.cold_functions},
        {"inlining_candidates", report.inlining_candidates},
        {"predictable_branches", report.predictable_branches},
        {"devirt_candidates", report.devirt_candidates},
        {"estimated_speedup_percent", report.estimated_speedup_percent}
    };
    
    // Export inlining decisions
    json inlining_json = json::array();
    for (const auto& d : report.inlining_decisions) {
        if (d.should_inline) {
            json dj;
            dj["caller"] = d.caller;
            dj["callee"] = d.callee;
            dj["call_site_id"] = d.call_site_id;
            dj["priority"] = d.priority;
            dj["expected_benefit"] = d.expected_benefit;
            dj["reason"] = d.reason;
            inlining_json.push_back(dj);
        }
    }
    j["inlining_decisions"] = inlining_json;
    
    // Export branch predictions
    json branch_json = json::array();
    for (const auto& h : report.branch_predictions) {
        json hj;
        hj["function"] = h.function;
        hj["branch_id"] = h.branch_id;
        hj["block_id"] = h.block_id;
        hj["likely_taken"] = h.likely_taken;
        hj["confidence"] = h.confidence;
        hj["taken_probability"] = h.taken_probability;
        hj["reason"] = h.reason;
        branch_json.push_back(hj);
    }
    j["branch_predictions"] = branch_json;
    
    // Export loop hints
    json loop_json = json::array();
    for (const auto& h : report.loop_hints) {
        json hj;
        hj["function"] = h.function;
        hj["loop_header"] = h.loop_header_block;
        hj["should_unroll"] = h.should_unroll;
        hj["unroll_factor"] = h.unroll_factor;
        hj["should_vectorize"] = h.should_vectorize;
        hj["expected_benefit"] = h.expected_benefit;
        hj["reason"] = h.reason;
        loop_json.push_back(hj);
    }
    j["loop_hints"] = loop_json;
    
    // Export devirtualization hints
    json devirt_json = json::array();
    for (const auto& h : report.devirt_hints) {
        json hj;
        hj["function"] = h.function;
        hj["call_site_id"] = h.call_site_id;
        hj["likely_target"] = h.likely_target;
        hj["confidence"] = h.confidence;
        hj["call_count"] = h.call_count;
        hj["should_specialize"] = h.should_specialize;
        hj["reason"] = h.reason;
        devirt_json.push_back(hj);
    }
    j["devirtualization_hints"] = devirt_json;
    
    // Write to file
    std::ofstream out(filename);
    if (!out) return false;
    
    out << j.dump(4);
    return true;
}

} // namespace polyglot::pgo
