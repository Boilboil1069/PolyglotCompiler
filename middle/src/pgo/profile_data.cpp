/**
 * PGO profile data implementation
 */

#include "middle/include/pgo/profile_data.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <numeric>

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

// ============ ProfileData implementation ============

void ProfileData::AddFunctionProfile(const FunctionProfile& profile) {
    functions_[profile.function_name] = profile;
}

const FunctionProfile* ProfileData::GetFunctionProfile(
    const std::string& name) const {
    auto it = functions_.find(name);
    return it != functions_.end() ? &it->second : nullptr;
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

std::vector<PGOOptimizer::InliningDecision> 
PGOOptimizer::MakeInliningDecisions() const {
    std::vector<InliningDecision> decisions;
    
    // Iterate over all function profile data
    for (size_t i = 0; i < profile_.GetFunctionCount(); ++i) {
        // Note: must iterate via the name list because functions_ is private
        // Temporarily skip the inlining decision logic here
    }
    
    // TODO: Implement profile-guided inlining decisions
    /*
    for (const auto& call_site : profile.call_sites) {
            InliningDecision decision;
            decision.caller = func_name;
            decision.call_site_id = call_site.call_site_id;
            decision.callee = call_site.callee_name;
            decision.should_inline = ShouldInline(call_site);
            
            if (decision.should_inline) {
                decision.reason = "Hot call site: " + 
                    std::to_string(call_site.call_count) + " calls";
            } else {
                decision.reason = "Cold or large callee";
            }
            
            decisions.push_back(decision);
        }
    }
    */
    
    return decisions;
}

bool PGOOptimizer::ShouldInline(const CallSiteProfile& call_site) const {
    // Simple heuristic: inline if call count > 1000
    return call_site.call_count > 1000;
}

std::vector<PGOOptimizer::BranchPredictionHint>
PGOOptimizer::GetBranchPredictions() const {
    std::vector<BranchPredictionHint> hints;
    
    // TODO: Implement branch prediction hints
    // Need a public API to iterate functions_
    /*
    for (const auto& [func_name, profile] : profile_.functions_) {
        for (const auto& branch : profile.branches) {
            double prob = branch.TakenProbability();
            if (prob > 0.9 || prob < 0.1) {
                BranchPredictionHint hint;
                hint.function = func_name;
                hint.branch_id = branch.branch_id;
                hint.likely_taken = prob > 0.5;
                hint.confidence = std::abs(prob - 0.5) * 2.0;
                hints.push_back(hint);
            }
        }
    }
    */
    
    return hints;
}

} // namespace polyglot::pgo
