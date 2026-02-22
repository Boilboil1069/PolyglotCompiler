#include "middle/include/lto/link_time_optimizer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <stack>
#include <thread>
#include <future>
#include <numeric>

#include "common/include/ir/ir_parser.h"

namespace polyglot::lto {

using Clock = std::chrono::steady_clock;

// ===================== LatticeValue =====================

LatticeValue LatticeValue::Meet(const LatticeValue& other) const {
  // Top ⊓ x = x
  if (kind == Kind::kTop) return other;
  if (other.kind == Kind::kTop) return *this;
  
  // Bottom ⊓ x = Bottom
  if (kind == Kind::kBottom || other.kind == Kind::kBottom) {
    return Bottom();
  }
  
  // Constant ⊓ Constant = Constant if same, Bottom otherwise
  if (is_float != other.is_float) return Bottom();
  
  if (is_float) {
    return (float_value == other.float_value) ? *this : Bottom();
  } else {
    return (int_value == other.int_value) ? *this : Bottom();
  }
}

bool LatticeValue::operator==(const LatticeValue& other) const {
  if (kind != other.kind) return false;
  if (kind != Kind::kConstant) return true;
  if (is_float != other.is_float) return false;
  return is_float ? (float_value == other.float_value) : (int_value == other.int_value);
}

// ===================== LTOModule =====================

bool LTOModule::SaveBitcode(const std::string &filename) const {
  std::ofstream out(filename, std::ios::binary);
  if (!out) return false;

  // Write module header
  out << "module " << module_name << "\n";
  out << functions.size() << ' ' << globals.size() << "\n";
  
  // Write function information
  for (const auto &fn : functions) {
    out << fn.name << "\n";
    // Write function body info
    out << fn.blocks.size() << "\n";
    for (const auto &block : fn.blocks) {
      if (!block) continue;
      out << block->name << " " << block->instructions.size() << "\n";
      // Serialize each instruction: name, type kind, operand count, operands
      for (const auto &inst : block->instructions) {
        if (!inst) {
          out << "_ 0 0\n";
          continue;
        }
        // Emit instruction name (use _ for empty names)
        std::string inst_name = inst->name.empty() ? "_" : inst->name;
        out << inst_name << " "
            << static_cast<int>(inst->type.kind) << " "
            << inst->operands.size();
        for (const auto &op : inst->operands) {
          out << " " << (op.empty() ? "_" : op);
        }
        out << "\n";
      }
    }
  }
  
  // Write global information
  for (const auto &gv : globals) {
    out << gv.name << "\n";
  }
  
  // Write entry points
  out << entry_points.size() << "\n";
  for (const auto &ep : entry_points) {
    out << ep << "\n";
  }

  return static_cast<bool>(out);
}

bool LTOModule::LoadBitcode(const std::string &filename) {
  std::ifstream in(filename, std::ios::binary);
  if (!in) return false;

  functions.clear();
  globals.clear();
  entry_points.clear();

  std::string header;
  if (!(in >> header >> module_name)) return false;
  
  size_t fn_count = 0, gv_count = 0;
  if (!(in >> fn_count >> gv_count)) return false;
  std::string line;
  std::getline(in, line);  // consume endline

  // Read functions
  for (size_t i = 0; i < fn_count; ++i) {
    if (!std::getline(in, line)) break;
    if (line.empty()) {
      --i;
      continue;
    }
    ir::Function fn;
    fn.name = line;
    
    // Try to read block count
    size_t block_count = 0;
    if (in >> block_count) {
      std::getline(in, line);  // consume endline
      for (size_t b = 0; b < block_count; ++b) {
        std::string block_name;
        size_t inst_count = 0;
        if (in >> block_name >> inst_count) {
          auto block = std::make_shared<ir::BasicBlock>();
          block->name = block_name;
          std::getline(in, line);  // consume endline after block header
          // Deserialize instruction data
          for (size_t inst_idx = 0; inst_idx < inst_count; ++inst_idx) {
            auto instruction = std::make_shared<ir::Instruction>();
            std::string inst_name;
            int type_kind = 0;
            size_t op_count = 0;
            if (in >> inst_name >> type_kind >> op_count) {
              instruction->name = (inst_name == "_") ? "" : inst_name;
              instruction->type = ir::IRType();
              instruction->type.kind = static_cast<ir::IRTypeKind>(type_kind);
              for (size_t op_i = 0; op_i < op_count; ++op_i) {
                std::string operand;
                if (in >> operand) {
                  instruction->operands.push_back(
                      (operand == "_") ? "" : operand);
                }
              }
            }
            std::getline(in, line);  // consume endline
            block->instructions.push_back(instruction);
          }
          fn.blocks.push_back(block);
        } else {
          std::getline(in, line);  // consume endline
        }
      }
    }
    
    functions.push_back(std::move(fn));
  }

  // Read globals
  for (size_t i = 0; i < gv_count; ++i) {
    if (!std::getline(in, line)) break;
    if (line.empty()) {
      --i;
      continue;
    }
    ir::GlobalValue gv;
    gv.name = line;
    globals.push_back(std::move(gv));
  }
  
  // Read entry points
  size_t ep_count = 0;
  if (in >> ep_count) {
    std::getline(in, line);  // consume endline
    for (size_t i = 0; i < ep_count; ++i) {
      if (std::getline(in, line) && !line.empty()) {
        entry_points.insert(line);
      }
    }
  }

  return true;
}

ir::Function* LTOModule::GetFunction(const std::string& name) {
  for (auto& fn : functions) {
    if (fn.name == name) return &fn;
  }
  return nullptr;
}

const ir::Function* LTOModule::GetFunction(const std::string& name) const {
  for (const auto& fn : functions) {
    if (fn.name == name) return &fn;
  }
  return nullptr;
}

size_t LTOModule::GetTotalInstructionCount() const {
  size_t total = 0;
  for (const auto& fn : functions) {
    for (const auto& block : fn.blocks) {
      if (block) {
        total += block->instructions.size();
        total += block->phis.size();
        if (block->terminator) ++total;
      }
    }
  }
  return total;
}

// ===================== LTOContext =====================

void LTOContext::AddModule(std::unique_ptr<LTOModule> module) {
  modules_.push_back(std::move(module));
  auto *stored = modules_.back().get();
  
  // Build function and global lookup maps
  for (auto &fn : stored->functions) {
    function_map_[fn.name] = &fn;
  }
  for (auto &gv : stored->globals) {
    global_map_[gv.name] = &gv;
  }
}

ir::Function *LTOContext::FindFunction(const std::string &name) {
  auto it = function_map_.find(name);
  return it == function_map_.end() ? nullptr : it->second;
}

const ir::Function *LTOContext::FindFunction(const std::string &name) const {
  auto it = function_map_.find(name);
  return it == function_map_.end() ? nullptr : it->second;
}

ir::GlobalValue *LTOContext::FindGlobal(const std::string &name) {
  auto it = global_map_.find(name);
  return it == global_map_.end() ? nullptr : it->second;
}

const ir::GlobalValue *LTOContext::FindGlobal(const std::string &name) const {
  auto it = global_map_.find(name);
  return it == global_map_.end() ? nullptr : it->second;
}

void LTOContext::RebuildIndexes() {
  function_map_.clear();
  global_map_.clear();
  for (auto &mod : modules_) {
    for (auto &fn : mod->functions) function_map_[fn.name] = &fn;
    for (auto &gv : mod->globals) global_map_[gv.name] = &gv;
  }
}

std::set<std::string> LTOContext::GetEntryPoints() const {
  std::set<std::string> entries;
  for (const auto& mod : modules_) {
    entries.insert(mod->entry_points.begin(), mod->entry_points.end());
    // Also include exported symbols as potential entry points
    for (const auto& [name, is_public] : mod->exported_symbols) {
      if (is_public) entries.insert(name);
    }
  }
  // "main" is always an entry point if it exists
  if (function_map_.count("main")) {
    entries.insert("main");
  }
  return entries;
}

LTOContext::CallGraph LTOContext::BuildCallGraph() const {
  CallGraph graph;
  
  // First pass: create nodes for all functions
  for (const auto &mod : modules_) {
    for (const auto &fn : mod->functions) {
      auto &node = graph.nodes[fn.name];
      node.function_name = fn.name;
      node.block_count = fn.blocks.size();
      node.is_entry_point = mod->entry_points.count(fn.name) > 0;
      
      // Calculate instruction count
      size_t inst_count = 0;
      for (const auto& block : fn.blocks) {
        if (block) {
          inst_count += block->instructions.size();
          inst_count += block->phis.size();
          if (block->terminator) ++inst_count;
        }
      }
      node.instruction_count = inst_count;
    }
  }
  
  // Second pass: extract call edges by scanning instructions
  for (const auto &mod : modules_) {
    for (const auto &fn : mod->functions) {
      for (const auto& block : fn.blocks) {
        if (!block) continue;
        
        for (const auto& inst : block->instructions) {
          auto* call = dynamic_cast<ir::CallInstruction*>(inst.get());
          if (call && !call->callee.empty()) {
            // Record call edge
            graph.nodes[fn.name].callees.push_back(call->callee);
            if (graph.nodes.count(call->callee)) {
              graph.nodes[call->callee].callers.push_back(fn.name);
            }
            
            // Create call site record
            CallGraph::CallSite site;
            site.caller = fn.name;
            site.callee = call->callee;
            site.is_indirect = call->is_indirect;
            graph.call_sites.push_back(site);
          }
        }
        
        // Check terminator for invoke instructions
        if (auto* invoke = dynamic_cast<ir::InvokeInstruction*>(block->terminator.get())) {
          if (!invoke->callee.empty()) {
            graph.nodes[fn.name].callees.push_back(invoke->callee);
            if (graph.nodes.count(invoke->callee)) {
              graph.nodes[invoke->callee].callers.push_back(fn.name);
            }
            
            CallGraph::CallSite site;
            site.caller = fn.name;
            site.callee = invoke->callee;
            site.is_indirect = invoke->is_indirect;
            graph.call_sites.push_back(site);
          }
        }
      }
    }
  }
  
  // Third pass: detect recursive functions using SCC analysis
  auto sccs = graph.GetSCCs();
  for (const auto& scc : sccs) {
    if (scc.size() > 1) {
      // All functions in a non-trivial SCC are recursive
      for (const auto& fn : scc) {
        if (graph.nodes.count(fn)) {
          graph.nodes[fn].is_recursive = true;
        }
      }
    } else if (scc.size() == 1) {
      // Check for self-recursion
      const auto& fn = scc[0];
      if (graph.nodes.count(fn)) {
        const auto& callees = graph.nodes[fn].callees;
        if (std::find(callees.begin(), callees.end(), fn) != callees.end()) {
          graph.nodes[fn].is_recursive = true;
        }
      }
    }
  }
  
  return graph;
}

std::vector<std::string> LTOContext::CallGraph::GetRoots() const {
  std::vector<std::string> roots;
  for (const auto &[name, node] : nodes) {
    if (node.callers.empty() || node.is_entry_point) {
      roots.push_back(name);
    }
  }
  return roots;
}

std::vector<std::string> LTOContext::CallGraph::GetLeaves() const {
  std::vector<std::string> leaves;
  for (const auto &[name, node] : nodes) {
    if (node.callees.empty()) {
      leaves.push_back(name);
    }
  }
  return leaves;
}

std::vector<std::vector<std::string>> LTOContext::CallGraph::GetSCCs() const {
  // Kosaraju's algorithm for finding strongly connected components
  std::vector<std::vector<std::string>> sccs;
  std::set<std::string> visited;
  std::stack<std::string> finish_stack;
  
  // First DFS to compute finish times
  std::function<void(const std::string&)> dfs1 = [&](const std::string& v) {
    if (visited.count(v)) return;
    visited.insert(v);
    
    auto it = nodes.find(v);
    if (it != nodes.end()) {
      for (const auto& callee : it->second.callees) {
        dfs1(callee);
      }
    }
    finish_stack.push(v);
  };
  
  for (const auto& [name, _] : nodes) {
    dfs1(name);
  }
  
  // Second DFS on reverse graph
  visited.clear();
  
  std::function<void(const std::string&, std::vector<std::string>&)> dfs2 = 
    [&](const std::string& v, std::vector<std::string>& scc) {
    if (visited.count(v)) return;
    visited.insert(v);
    scc.push_back(v);
    
    auto it = nodes.find(v);
    if (it != nodes.end()) {
      for (const auto& caller : it->second.callers) {
        dfs2(caller, scc);
      }
    }
  };
  
  while (!finish_stack.empty()) {
    std::string v = finish_stack.top();
    finish_stack.pop();
    
    if (!visited.count(v)) {
      std::vector<std::string> scc;
      dfs2(v, scc);
      if (!scc.empty()) {
        sccs.push_back(std::move(scc));
      }
    }
  }
  
  return sccs;
}

std::vector<std::string> LTOContext::CallGraph::GetReversePostOrder() const {
  std::vector<std::string> order;
  std::set<std::string> visited;
  
  std::function<void(const std::string&)> dfs = [&](const std::string& v) {
    if (visited.count(v)) return;
    visited.insert(v);
    
    auto it = nodes.find(v);
    if (it != nodes.end()) {
      for (const auto& callee : it->second.callees) {
        dfs(callee);
      }
    }
    order.push_back(v);
  };
  
  // Start from all roots
  for (const auto& root : GetRoots()) {
    dfs(root);
  }
  
  // Visit any remaining nodes
  for (const auto& [name, _] : nodes) {
    dfs(name);
  }
  
  std::reverse(order.begin(), order.end());
  return order;
}

bool LTOContext::CallGraph::IsReachable(const std::string& from, const std::string& to) const {
  if (from == to) return true;
  
  std::set<std::string> visited;
  std::queue<std::string> worklist;
  worklist.push(from);
  
  while (!worklist.empty()) {
    std::string current = worklist.front();
    worklist.pop();
    
    if (current == to) return true;
    if (visited.count(current)) continue;
    visited.insert(current);
    
    auto it = nodes.find(current);
    if (it != nodes.end()) {
      for (const auto& callee : it->second.callees) {
        worklist.push(callee);
      }
    }
  }
  
  return false;
}

// ===================== CrossModuleInliner =====================

void CrossModuleInliner::Run() {
  auto candidates = FindInlineCandidates();
  
  // Sort candidates by benefit/cost ratio (best first)
  std::sort(candidates.begin(), candidates.end(), 
    [](const InlineCandidate& a, const InlineCandidate& b) {
      if (a.inline_cost == 0 || b.inline_cost == 0) {
        return a.should_inline && !b.should_inline;
      }
      double ratio_a = static_cast<double>(a.inline_benefit) / a.inline_cost;
      double ratio_b = static_cast<double>(b.inline_benefit) / b.inline_cost;
      return ratio_a > ratio_b;
    });
  
  for (const auto &cand : candidates) {
    if (!cand.should_inline) continue;
    
    auto *caller = context_.FindFunction(cand.caller);
    auto *callee = context_.FindFunction(cand.callee);
    
    if (caller && callee && caller != callee) {
      InlineFunction(*caller, *callee);
      ++inlined_count_;
    }
  }
}

std::vector<CrossModuleInliner::InlineCandidate> CrossModuleInliner::FindInlineCandidates() const {
  std::vector<InlineCandidate> result;
  auto cg = context_.BuildCallGraph();
  
  for (const auto &[fn_name, node] : cg.nodes) {
    for (const auto &callee : node.callees) {
      auto *caller_fn = context_.FindFunction(fn_name);
      auto *callee_fn = context_.FindFunction(callee);
      
      if (!caller_fn || !callee_fn) continue;
      if (fn_name == callee) continue;  // Skip self-recursion
      
      InlineCandidate cand;
      cand.caller = fn_name;
      cand.callee = callee;
      
      // Count call sites to this callee from this caller
      cand.call_site_count = std::count(node.callees.begin(), node.callees.end(), callee);
      
      // Get callee size information
      cand.callee_size = CalculateFunctionSize(*callee_fn);
      cand.callee_block_count = callee_fn->blocks.size();
      
      // Check if callee is recursive
      if (cg.nodes.count(callee)) {
        cand.is_recursive = cg.nodes.at(callee).is_recursive;
      }
      
      // Check for hot call sites (from PGO or heuristics)
      // For now, consider functions called only once as "hot"
      if (cg.nodes.count(callee)) {
        cand.is_hot = cg.nodes.at(callee).callers.size() == 1;
      }
      
      // Calculate cost and benefit
      cand.inline_cost = CalculateInlineCost(*caller_fn, *callee_fn, 
                                             cand.call_site_count, cand.is_recursive);
      cand.inline_benefit = CalculateInlineBenefit(*caller_fn, *callee_fn);
      
      // Decide whether to inline
      cand.should_inline = ShouldInline(*caller_fn, *callee_fn);
      
      result.push_back(cand);
    }
  }
  
  return result;
}

size_t CrossModuleInliner::CalculateFunctionSize(const ir::Function& func) const {
  size_t size = 0;
  for (const auto& block : func.blocks) {
    if (block) {
      size += block->instructions.size();
      size += block->phis.size();
      if (block->terminator) ++size;
    }
  }
  return size;
}

size_t CrossModuleInliner::CalculateInlineCost(const ir::Function& caller,
                                               const ir::Function& callee,
                                               size_t call_site_count,
                                               bool is_recursive) const {
  (void)caller;  // May be used for more advanced heuristics
  
  size_t callee_size = CalculateFunctionSize(callee);
  size_t base_cost = callee_size * InlineCostModel::kBaseInstructionCost;
  
  // Multiply by call site count (inlining happens at each site)
  base_cost *= std::max(call_site_count, static_cast<size_t>(1));
  
  // Add complexity penalty for functions with many blocks
  if (callee.blocks.size() > 5) {
    base_cost += (callee.blocks.size() - 5) * InlineCostModel::kComplexityPenalty;
  }
  
  // Add penalty for recursive functions
  if (is_recursive) {
    base_cost += InlineCostModel::kRecursivePenalty;
  }
  
  return base_cost;
}

size_t CrossModuleInliner::CalculateInlineBenefit(const ir::Function& caller,
                                                  const ir::Function& callee) const {
  size_t benefit = 0;
  size_t callee_size = CalculateFunctionSize(callee);
  
  // Small function bonus
  if (callee_size <= InlineCostModel::kSmallFunctionThreshold) {
    benefit += InlineCostModel::kSmallFunctionBonus;
  }
  
  // Check if callee is called from only one place (single call site bonus)
  auto cg = context_.BuildCallGraph();
  if (cg.nodes.count(callee.name)) {
    const auto& node = cg.nodes.at(callee.name);
    if (node.callers.size() == 1) {
      benefit += InlineCostModel::kSingleCallSiteBonus;
    }
  }
  
  // Bonus for functions with constant arguments (better optimization after inlining)
  // This is a heuristic - functions with few parameters tend to benefit more
  if (callee.params.size() <= 2) {
    benefit += 25;
  }
  
  // Bonus for leaf functions (no calls inside)
  if (cg.nodes.count(callee.name) && cg.nodes.at(callee.name).callees.empty()) {
    benefit += 30;
  }
  
  // Consider caller context - larger callers benefit more from inlining
  size_t caller_size = CalculateFunctionSize(caller);
  if (caller_size > 50) {
    benefit += 10;  // Amortized call overhead over more instructions
  }
  
  return benefit;
}

bool CrossModuleInliner::ShouldInline(const ir::Function &caller, const ir::Function &callee) const {
  (void)caller;
  
  size_t callee_size = CalculateFunctionSize(callee);
  
  // Always inline tiny functions (threshold: 5 instructions)
  if (callee_size <= 5) {
    return true;
  }
  
  // Check recursive functions
  auto cg = context_.BuildCallGraph();
  if (cg.nodes.count(callee.name) && cg.nodes.at(callee.name).is_recursive) {
    // Only inline recursive functions if they're very small
    return callee_size <= 10;
  }
  
  // Use cost model
  size_t cost = CalculateInlineCost(caller, callee, 1, false);
  size_t benefit = CalculateInlineBenefit(caller, callee);
  
  // Inline if benefit outweighs cost or if under threshold
  if (benefit >= cost) {
    return true;
  }
  
  // Inline if total cost is under threshold
  return cost <= inline_threshold_;
}

void CrossModuleInliner::InlineFunction(ir::Function &caller, const ir::Function &callee) {
  // Clone callee blocks into caller
  std::map<std::string, std::string> name_map;
  std::string prefix = caller.name + "_inl_" + callee.name + "_";
  
  // First, create renamed versions of callee's blocks
  std::vector<std::shared_ptr<ir::BasicBlock>> inlined_blocks;
  
  for (const auto &block : callee.blocks) {
    if (!block) continue;
    
    auto cloned = std::make_shared<ir::BasicBlock>();
    cloned->name = prefix + block->name;
    name_map[block->name] = cloned->name;
    
    // Clone instructions with renamed operands
    CloneInstructions(*cloned, *block, prefix, name_map);
    
    inlined_blocks.push_back(std::move(cloned));
  }
  
  // Add inlined blocks to caller
  for (auto& block : inlined_blocks) {
    caller.blocks.push_back(std::move(block));
  }
}

void CrossModuleInliner::CloneInstructions(ir::BasicBlock& target,
                                           const ir::BasicBlock& source,
                                           const std::string& prefix,
                                           std::map<std::string, std::string>& name_map) {
  // Clone phi nodes
  for (const auto& phi : source.phis) {
    auto cloned = std::make_shared<ir::PhiInstruction>();
    cloned->name = prefix + phi->name;
    cloned->type = phi->type;
    cloned->parent = &target;
    
    // Update phi incomings with renamed blocks
    for (const auto& [pred, val] : phi->incomings) {
      std::string new_val = val;
      if (name_map.count(val)) {
        new_val = name_map[val];
      }
      cloned->incomings.emplace_back(nullptr, new_val);  // Pred will be fixed later
    }
    
    name_map[phi->name] = cloned->name;
    target.phis.push_back(cloned);
  }
  
  // Clone regular instructions
  for (const auto& inst : source.instructions) {
    auto cloned = std::make_shared<ir::Instruction>();
    cloned->name = inst->name.empty() ? "" : prefix + inst->name;
    cloned->type = inst->type;
    cloned->parent = &target;
    
    // Rename operands
    for (const auto& op : inst->operands) {
      if (name_map.count(op)) {
        cloned->operands.push_back(name_map[op]);
      } else {
        cloned->operands.push_back(op);
      }
    }
    
    if (!cloned->name.empty()) {
      name_map[inst->name] = cloned->name;
    }
    target.instructions.push_back(cloned);
  }
  
  // Clone terminator
  if (source.terminator) {
    auto cloned = std::make_shared<ir::Instruction>();
    cloned->type = source.terminator->type;
    cloned->parent = &target;
    
    for (const auto& op : source.terminator->operands) {
      if (name_map.count(op)) {
        cloned->operands.push_back(name_map[op]);
      } else {
        cloned->operands.push_back(op);
      }
    }
    
    target.terminator = cloned;
  }
}

// ===================== GlobalDeadCodeElimination =====================

void GlobalDeadCodeElimination::Run() {
  auto reachable = MarkReachableSymbols();
  
  for (auto &mod : context_.MutableModules()) {
    size_t before_funcs = mod->functions.size();
    size_t before_globals = mod->globals.size();
    
    // Remove unreachable functions
    auto &funcs = mod->functions;
    funcs.erase(std::remove_if(funcs.begin(), funcs.end(), 
      [&](const ir::Function &fn) {
        // Keep if reachable or if reachable set is empty (conservative)
        return !reachable.empty() && reachable.count(fn.name) == 0;
      }),
      funcs.end());

    // Remove unreachable globals
    auto &globals = mod->globals;
    globals.erase(std::remove_if(globals.begin(), globals.end(), 
      [&](const ir::GlobalValue &gv) {
        return !reachable.empty() && reachable.count(gv.name) == 0;
      }),
      globals.end());
    
    removed_functions_ += before_funcs - mod->functions.size();
    removed_globals_ += before_globals - mod->globals.size();
  }
  
  context_.RebuildIndexes();
}

std::set<std::string> GlobalDeadCodeElimination::MarkReachableSymbols() const {
  std::set<std::string> reachable;
  
  // Get entry points
  auto entry_points = context_.GetEntryPoints();
  auto cg = context_.BuildCallGraph();
  
  // If no entry points defined, mark all exported symbols as entry points
  if (entry_points.empty()) {
    for (const auto &mod : context_.GetModules()) {
      for (const auto &[name, is_public] : mod->exported_symbols) {
        if (is_public) {
          entry_points.insert(name);
        }
      }
      // Also treat all functions as potentially reachable if no exports defined
      if (mod->exported_symbols.empty()) {
        for (const auto &fn : mod->functions) {
          entry_points.insert(fn.name);
        }
      }
    }
  }
  
  // Mark all symbols reachable from entry points
  for (const auto& entry : entry_points) {
    MarkReachable(entry, reachable, cg);
  }
  
  return reachable;
}

void GlobalDeadCodeElimination::MarkReachable(const std::string& symbol,
                                              std::set<std::string>& reachable,
                                              const LTOContext::CallGraph& cg) const {
  if (reachable.count(symbol)) return;  // Already visited
  
  reachable.insert(symbol);
  
  // Mark all callees as reachable
  auto it = cg.nodes.find(symbol);
  if (it != cg.nodes.end()) {
    for (const auto& callee : it->second.callees) {
      MarkReachable(callee, reachable, cg);
    }
  }
  
  // Also check for referenced globals within the function
  auto* func = context_.FindFunction(symbol);
  if (func) {
    for (const auto& block : func->blocks) {
      if (!block) continue;
      
      // Check load/store instructions for global references
      for (const auto& inst : block->instructions) {
        for (const auto& op : inst->operands) {
          // If operand refers to a global, mark it reachable
          if (context_.FindGlobal(op)) {
            reachable.insert(op);
          }
        }
      }
    }
  }
}

// ===================== InterproceduralConstantPropagation =====================

void InterproceduralConstantPropagation::Run() {
  // Phase 1: Analyze which function arguments are always constant
  auto constant_args = AnalyzeConstantArgs();
  
  // Update statistics
  constants_propagated_ = constant_args.size();
  
  // Phase 2: Propagate constants within each function
  for (auto& mod : context_.MutableModules()) {
    for (auto& fn : mod->functions) {
      PropagateInFunction(fn);
    }
  }
  
  // Phase 3: Replace constant uses
  for (auto& mod : context_.MutableModules()) {
    for (auto& fn : mod->functions) {
      ReplaceConstantUses(fn);
    }
  }
}

std::vector<InterproceduralConstantPropagation::ConstantArgInfo> 
InterproceduralConstantPropagation::AnalyzeConstantArgs() const {
  std::vector<ConstantArgInfo> result;
  
  // For each function, analyze all call sites to determine constant arguments
  std::map<std::string, std::vector<std::vector<LatticeValue>>> arg_values;
  
  // Collect argument values at all call sites
  for (const auto& mod : context_.GetModules()) {
    for (const auto& fn : mod->functions) {
      for (const auto& block : fn.blocks) {
        if (!block) continue;
        
        for (const auto& inst : block->instructions) {
          auto* call = dynamic_cast<ir::CallInstruction*>(inst.get());
          if (!call || call->callee.empty()) continue;
          
          // Get lattice values for each argument
          std::vector<LatticeValue> call_args;
          for (const auto& arg : call->operands) {
            // Check if argument is a known constant
            auto it = lattice_.find(arg);
            if (it != lattice_.end()) {
              call_args.push_back(it->second);
            } else {
              // Try to parse as integer literal
              try {
                int64_t val = std::stoll(arg);
                call_args.push_back(LatticeValue::Constant(val));
              } catch (...) {
                call_args.push_back(LatticeValue::Bottom());  // Unknown value
              }
            }
          }
          
          arg_values[call->callee].push_back(std::move(call_args));
        }
      }
    }
  }
  
  // Compute meet over all call sites for each argument
  for (const auto& [fn_name, all_args] : arg_values) {
    if (all_args.empty()) continue;
    
    // Determine max argument count
    size_t max_args = 0;
    for (const auto& args : all_args) {
      max_args = std::max(max_args, args.size());
    }
    
    // Meet over all values for each argument position
    for (size_t i = 0; i < max_args; ++i) {
      LatticeValue merged = LatticeValue::Top();
      
      for (const auto& args : all_args) {
        if (i < args.size()) {
          merged = merged.Meet(args[i]);
        } else {
          merged = LatticeValue::Bottom();  // Missing argument
        }
      }
      
      if (merged.IsConstant()) {
        ConstantArgInfo info;
        info.function_name = fn_name;
        info.arg_index = i;
        info.value = merged;
        result.push_back(info);
      }
    }
  }
  
  return result;
}

void InterproceduralConstantPropagation::PropagateInFunction(ir::Function& func) {
  bool changed = true;
  
  // Iterate until fixed point
  while (changed) {
    changed = false;
    
    for (auto& block : func.blocks) {
      if (!block) continue;
      
      // Process phi nodes
      for (auto& phi : block->phis) {
        if (phi->name.empty()) continue;
        
        LatticeValue merged = LatticeValue::Top();
        for (const auto& [_, val] : phi->incomings) {
          auto it = lattice_.find(val);
          if (it != lattice_.end()) {
            merged = merged.Meet(it->second);
          } else {
            // Try to parse as constant
            try {
              int64_t v = std::stoll(val);
              merged = merged.Meet(LatticeValue::Constant(v));
            } catch (...) {
              merged = LatticeValue::Bottom();
            }
          }
        }
        
        auto& current = lattice_[phi->name];
        if (current != merged) {
          current = merged;
          changed = true;
        }
      }
      
      // Process regular instructions
      for (auto& inst : block->instructions) {
        if (inst->name.empty()) continue;
        
        // Handle binary instructions
        auto* binary = dynamic_cast<ir::BinaryInstruction*>(inst.get());
        if (binary && binary->operands.size() >= 2) {
          LatticeValue lhs = LatticeValue::Bottom();
          LatticeValue rhs = LatticeValue::Bottom();
          
          // Get LHS value
          auto lhs_it = lattice_.find(binary->operands[0]);
          if (lhs_it != lattice_.end()) {
            lhs = lhs_it->second;
          } else {
            try {
              int64_t v = std::stoll(binary->operands[0]);
              lhs = LatticeValue::Constant(v);
            } catch (...) {}
          }
          
          // Get RHS value
          auto rhs_it = lattice_.find(binary->operands[1]);
          if (rhs_it != lattice_.end()) {
            rhs = rhs_it->second;
          } else {
            try {
              int64_t v = std::stoll(binary->operands[1]);
              rhs = LatticeValue::Constant(v);
            } catch (...) {}
          }
          
          // Evaluate the operation
          LatticeValue result = EvaluateBinaryOp(binary->op, lhs, rhs);
          
          auto& current = lattice_[binary->name];
          if (current != result) {
            current = result;
            changed = true;
          }
        }
      }
    }
  }
}

LatticeValue InterproceduralConstantPropagation::EvaluateBinaryOp(
    ir::BinaryInstruction::Op op,
    const LatticeValue& lhs,
    const LatticeValue& rhs) const {
  
  // If either operand is bottom, result is bottom
  if (lhs.IsBottom() || rhs.IsBottom()) {
    return LatticeValue::Bottom();
  }
  
  // If either operand is top, result is top (not enough info)
  if (lhs.IsTop() || rhs.IsTop()) {
    return LatticeValue::Top();
  }
  
  // Both are constants - evaluate the operation
  if (!lhs.is_float && !rhs.is_float) {
    int64_t l = lhs.int_value;
    int64_t r = rhs.int_value;
    
    switch (op) {
      case ir::BinaryInstruction::Op::kAdd:
        return LatticeValue::Constant(l + r);
      case ir::BinaryInstruction::Op::kSub:
        return LatticeValue::Constant(l - r);
      case ir::BinaryInstruction::Op::kMul:
        return LatticeValue::Constant(l * r);
      case ir::BinaryInstruction::Op::kDiv:
      case ir::BinaryInstruction::Op::kSDiv:
        if (r == 0) return LatticeValue::Bottom();
        return LatticeValue::Constant(l / r);
      case ir::BinaryInstruction::Op::kUDiv:
        if (r == 0) return LatticeValue::Bottom();
        return LatticeValue::Constant(static_cast<int64_t>(
            static_cast<uint64_t>(l) / static_cast<uint64_t>(r)));
      case ir::BinaryInstruction::Op::kSRem:
      case ir::BinaryInstruction::Op::kRem:
        if (r == 0) return LatticeValue::Bottom();
        return LatticeValue::Constant(l % r);
      case ir::BinaryInstruction::Op::kURem:
        if (r == 0) return LatticeValue::Bottom();
        return LatticeValue::Constant(static_cast<int64_t>(
            static_cast<uint64_t>(l) % static_cast<uint64_t>(r)));
      case ir::BinaryInstruction::Op::kAnd:
        return LatticeValue::Constant(l & r);
      case ir::BinaryInstruction::Op::kOr:
        return LatticeValue::Constant(l | r);
      case ir::BinaryInstruction::Op::kXor:
        return LatticeValue::Constant(l ^ r);
      case ir::BinaryInstruction::Op::kShl:
        return LatticeValue::Constant(l << r);
      case ir::BinaryInstruction::Op::kLShr:
        return LatticeValue::Constant(static_cast<int64_t>(
            static_cast<uint64_t>(l) >> r));
      case ir::BinaryInstruction::Op::kAShr:
        return LatticeValue::Constant(l >> r);
      case ir::BinaryInstruction::Op::kCmpEq:
        return LatticeValue::Constant(l == r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpNe:
        return LatticeValue::Constant(l != r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpSlt:
      case ir::BinaryInstruction::Op::kCmpLt:
        return LatticeValue::Constant(l < r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpSle:
        return LatticeValue::Constant(l <= r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpSgt:
        return LatticeValue::Constant(l > r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpSge:
        return LatticeValue::Constant(l >= r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpUlt:
        return LatticeValue::Constant(
            static_cast<uint64_t>(l) < static_cast<uint64_t>(r) ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpUle:
        return LatticeValue::Constant(
            static_cast<uint64_t>(l) <= static_cast<uint64_t>(r) ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpUgt:
        return LatticeValue::Constant(
            static_cast<uint64_t>(l) > static_cast<uint64_t>(r) ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpUge:
        return LatticeValue::Constant(
            static_cast<uint64_t>(l) >= static_cast<uint64_t>(r) ? 1 : 0);
      default:
        return LatticeValue::Bottom();
    }
  }
  
  // Handle floating point operations
  if (lhs.is_float && rhs.is_float) {
    double l = lhs.float_value;
    double r = rhs.float_value;
    
    switch (op) {
      case ir::BinaryInstruction::Op::kFAdd:
        return LatticeValue::Constant(l + r);
      case ir::BinaryInstruction::Op::kFSub:
        return LatticeValue::Constant(l - r);
      case ir::BinaryInstruction::Op::kFMul:
        return LatticeValue::Constant(l * r);
      case ir::BinaryInstruction::Op::kFDiv:
        return LatticeValue::Constant(l / r);
      case ir::BinaryInstruction::Op::kCmpFoe:
        return LatticeValue::Constant(l == r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpFne:
        return LatticeValue::Constant(l != r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpFlt:
        return LatticeValue::Constant(l < r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpFle:
        return LatticeValue::Constant(l <= r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpFgt:
        return LatticeValue::Constant(l > r ? 1 : 0);
      case ir::BinaryInstruction::Op::kCmpFge:
        return LatticeValue::Constant(l >= r ? 1 : 0);
      default:
        return LatticeValue::Bottom();
    }
  }
  
  return LatticeValue::Bottom();
}

void InterproceduralConstantPropagation::ReplaceConstantUses(ir::Function& func) {
  for (auto& block : func.blocks) {
    if (!block) continue;
    
    // Replace in phi nodes
    for (auto& phi : block->phis) {
      for (auto& [pred, val] : phi->incomings) {
        auto it = lattice_.find(val);
        if (it != lattice_.end() && it->second.IsConstant() && !it->second.is_float) {
          val = std::to_string(it->second.int_value);
        }
      }
    }
    
    // Replace in regular instructions
    for (auto& inst : block->instructions) {
      for (auto& op : inst->operands) {
        auto it = lattice_.find(op);
        if (it != lattice_.end() && it->second.IsConstant() && !it->second.is_float) {
          op = std::to_string(it->second.int_value);
        }
      }
    }
    
    // Replace in terminator
    if (block->terminator) {
      for (auto& op : block->terminator->operands) {
        auto it = lattice_.find(op);
        if (it != lattice_.end() && it->second.IsConstant() && !it->second.is_float) {
          op = std::to_string(it->second.int_value);
        }
      }
    }
  }
}

// ===================== CrossModuleDevirtualization =====================

void CrossModuleDevirtualization::Run() {
  // Build class hierarchy first
  BuildClassHierarchy();
  
  // Process all call instructions in all functions
  for (auto& mod : context_.MutableModules()) {
    for (auto& fn : mod->functions) {
      for (auto& block : fn.blocks) {
        if (!block) continue;
        
        for (auto& inst : block->instructions) {
          auto* call = dynamic_cast<ir::CallInstruction*>(inst.get());
          if (call && TryDevirtualize(*call)) {
            ++devirtualized_count_;
          }
        }
      }
    }
  }
}

void CrossModuleDevirtualization::BuildClassHierarchy() {
  class_hierarchy_.clear();
  
  // Scan all modules for class/type definitions
  // This uses function naming conventions to infer class relationships
  // Format: ClassName::methodName or ClassName_methodName
  
  for (const auto& mod : context_.GetModules()) {
    for (const auto& fn : mod->functions) {
      // Parse class::method pattern
      size_t pos = fn.name.find("::");
      if (pos != std::string::npos) {
        std::string class_name = fn.name.substr(0, pos);
        std::string method_name = fn.name.substr(pos + 2);
        
        auto& info = class_hierarchy_[class_name];
        info.name = class_name;
        info.methods.insert(method_name);
      }
    }
    
    // Check for virtual table globals that can provide inheritance info
    for (const auto& gv : mod->globals) {
      // Look for vtable patterns like "_ZTV5Class" or "vtable_Class"
      if (gv.name.find("vtable") != std::string::npos ||
          gv.name.find("_ZTV") != std::string::npos) {
        // Extract class name and mark as having virtual methods
        // This is a simplified heuristic
      }
    }
  }
  
  // Build inheritance relationships
  // For each class, check if its methods are supersets of other classes
  for (auto& [name, info] : class_hierarchy_) {
    for (const auto& [other_name, other_info] : class_hierarchy_) {
      if (name == other_name) continue;
      
      // Check if name could be a derived class of other_name
      // by looking at method overlap and naming patterns
      bool is_derived = false;
      
      // Check naming convention: DerivedClass might contain BaseClass as substring
      if (name.find(other_name) != std::string::npos && name != other_name) {
        // Check if derived has all methods of base
        bool has_all = true;
        for (const auto& method : other_info.methods) {
          if (!info.methods.count(method)) {
            has_all = false;
            break;
          }
        }
        is_derived = has_all;
      }
      
      if (is_derived) {
        info.base_class = other_name;
        class_hierarchy_[other_name].derived_classes.push_back(name);
      }
    }
  }
}

bool CrossModuleDevirtualization::TryDevirtualize(ir::CallInstruction& call) {
  // Check if this is an indirect call that might be virtual
  if (!call.is_indirect) return false;
  
  // Parse the call target to determine class and method
  std::string callee = call.callee;
  size_t pos = callee.find("::");
  if (pos == std::string::npos) return false;
  
  std::string class_name = callee.substr(0, pos);
  std::string method_name = callee.substr(pos + 2);
  
  // Check if we have class hierarchy information
  auto it = class_hierarchy_.find(class_name);
  if (it == class_hierarchy_.end()) return false;
  
  const auto& class_info = it->second;
  
  // Strategy 1: Final class - no derived classes exist
  if (class_info.derived_classes.empty()) {
    // Convert to direct call
    call.is_indirect = false;
    return true;
  }
  
  // Strategy 2: Check for unique implementation
  auto unique_impl = GetUniqueImplementation(class_name, method_name);
  if (unique_impl) {
    call.callee = *unique_impl;
    call.is_indirect = false;
    return true;
  }
  
  return false;
}

std::optional<std::string> CrossModuleDevirtualization::GetUniqueImplementation(
    const std::string& class_name,
    const std::string& method_name) const {
  
  // Collect all implementations of the method in the class hierarchy
  std::vector<std::string> implementations;
  
  std::function<void(const std::string&)> collect_impls = [&](const std::string& name) {
    auto it = class_hierarchy_.find(name);
    if (it == class_hierarchy_.end()) return;
    
    const auto& info = it->second;
    
    // Check if this class has an implementation
    if (info.methods.count(method_name)) {
      std::string full_name = name + "::" + method_name;
      implementations.push_back(full_name);
    }
    
    // Check derived classes
    for (const auto& derived : info.derived_classes) {
      collect_impls(derived);
    }
  };
  
  collect_impls(class_name);
  
  // If there's exactly one implementation, return it
  if (implementations.size() == 1) {
    return implementations[0];
  }
  
  return std::nullopt;
}

// ===================== CrossModuleGVN =====================

size_t CrossModuleGVN::ExpressionHash::operator()(const Expression& expr) const {
  size_t hash = std::hash<std::string>{}(expr.opcode);
  for (size_t vn : expr.operand_vns) {
    hash ^= std::hash<size_t>{}(vn) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return hash;
}

void CrossModuleGVN::Run() {
  // Run GVN on each function
  for (auto& mod : context_.MutableModules()) {
    for (auto& fn : mod->functions) {
      RunOnFunction(fn);
    }
  }
}

void CrossModuleGVN::RunOnFunction(ir::Function& func) {
  value_numbers_.clear();
  expression_to_vn_.clear();
  vn_to_canonical_.clear();
  next_value_number_ = 1;
  
  std::map<std::string, std::string> replacements;
  
  // Process blocks in order
  for (auto& block : func.blocks) {
    if (!block) continue;
    
    // Process phi nodes - each gets a unique value number
    for (auto& phi : block->phis) {
      if (!phi->name.empty()) {
        value_numbers_[phi->name] = next_value_number_;
        vn_to_canonical_[next_value_number_] = phi->name;
        ++next_value_number_;
      }
    }
    
    // Process instructions
    for (auto& inst : block->instructions) {
      if (inst->name.empty()) continue;
      
      // Check if this is a pure instruction
      if (!IsPure(inst.get())) {
        // Non-pure instructions get unique value numbers
        value_numbers_[inst->name] = next_value_number_;
        vn_to_canonical_[next_value_number_] = inst->name;
        ++next_value_number_;
        continue;
      }
      
      // Create expression for this instruction
      Expression expr = CreateExpression(inst.get());
      
      // Check if we've seen this expression before
      auto it = expression_to_vn_.find(expr);
      if (it != expression_to_vn_.end()) {
        // Found redundant computation
        size_t vn = it->second;
        value_numbers_[inst->name] = vn;
        
        // Record replacement
        std::string canonical = vn_to_canonical_[vn];
        if (canonical != inst->name) {
          replacements[inst->name] = canonical;
          ++eliminated_count_;
        }
      } else {
        // New expression
        size_t vn = next_value_number_++;
        expression_to_vn_[expr] = vn;
        value_numbers_[inst->name] = vn;
        vn_to_canonical_[vn] = inst->name;
      }
    }
  }
  
  // Apply replacements
  if (!replacements.empty()) {
    for (auto& block : func.blocks) {
      if (!block) continue;
      
      // Update phi node operands
      for (auto& phi : block->phis) {
        for (auto& [pred, val] : phi->incomings) {
          auto it = replacements.find(val);
          if (it != replacements.end()) {
            val = it->second;
          }
        }
      }
      
      // Update instruction operands
      for (auto& inst : block->instructions) {
        for (auto& op : inst->operands) {
          auto it = replacements.find(op);
          if (it != replacements.end()) {
            op = it->second;
          }
        }
      }
      
      // Update terminator operands
      if (block->terminator) {
        for (auto& op : block->terminator->operands) {
          auto it = replacements.find(op);
          if (it != replacements.end()) {
            op = it->second;
          }
        }
      }
    }
    
    // Mark replaced instructions as dead
    for (auto& block : func.blocks) {
      if (!block) continue;
      for (auto& inst : block->instructions) {
        if (replacements.count(inst->name)) {
          inst->is_dead = true;
        }
      }
    }
  }
}

CrossModuleGVN::Expression CrossModuleGVN::CreateExpression(const ir::Instruction* inst) const {
  Expression expr;
  
  // Determine opcode string
  if (auto* binary = dynamic_cast<const ir::BinaryInstruction*>(inst)) {
    expr.opcode = "binary_" + std::to_string(static_cast<int>(binary->op));
  } else if (dynamic_cast<const ir::LoadInstruction*>(inst)) {
    expr.opcode = "load";
  } else if (dynamic_cast<const ir::CastInstruction*>(inst)) {
    expr.opcode = "cast";
  } else if (dynamic_cast<const ir::GetElementPtrInstruction*>(inst)) {
    expr.opcode = "gep";
  } else {
    expr.opcode = "unknown";
  }
  
  // Add type information
  expr.opcode += "_" + std::to_string(static_cast<int>(inst->type.kind));
  
  // Get value numbers for operands
  for (const auto& op : inst->operands) {
    auto it = value_numbers_.find(op);
    if (it != value_numbers_.end()) {
      expr.operand_vns.push_back(it->second);
    } else {
      // For constants or unknown values, use a hash of the string
      expr.operand_vns.push_back(std::hash<std::string>{}(op));
    }
  }
  
  return expr;
}

bool CrossModuleGVN::IsPure(const ir::Instruction* inst) const {
  // Pure instructions have no side effects and produce the same result
  // for the same inputs
  
  // Calls are not pure (may have side effects)
  if (dynamic_cast<const ir::CallInstruction*>(inst)) return false;
  
  // Stores are not pure
  if (dynamic_cast<const ir::StoreInstruction*>(inst)) return false;
  
  // Allocas are not pure (each one creates a new address)
  if (dynamic_cast<const ir::AllocaInstruction*>(inst)) return false;
  
  // Terminators are not pure
  if (inst->IsTerminator()) return false;
  
  // Loads from volatile memory are not pure
  // For simplicity, we treat all loads as potentially non-pure
  // A more sophisticated analysis would track memory state
  if (dynamic_cast<const ir::LoadInstruction*>(inst)) return false;
  
  // Binary operations, casts, GEPs are pure
  if (dynamic_cast<const ir::BinaryInstruction*>(inst)) return true;
  if (dynamic_cast<const ir::CastInstruction*>(inst)) return true;
  if (dynamic_cast<const ir::GetElementPtrInstruction*>(inst)) return true;
  
  // Default: not pure
  return false;
}

// ===================== GlobalOptimizer =====================
namespace {
size_t CountFunctions(const LTOContext &ctx) {
  size_t total = 0;
  for (const auto &mod : ctx.GetModules()) total += mod->functions.size();
  return total;
}

size_t CountGlobals(const LTOContext &ctx) {
  size_t total = 0;
  for (const auto &mod : ctx.GetModules()) total += mod->globals.size();
  return total;
}
}  // namespace

void GlobalOptimizer::Optimize() {
  stats_ = {};
  auto start = Clock::now();
  RunInlining();
  RunDeadCodeElimination();
  RunConstantPropagation();
  RunDevirtualization();
  RunGlobalValueNumbering();
  (void)start;
}

void GlobalOptimizer::RunInlining() {
  CrossModuleInliner inliner(context_);
  auto candidates = inliner.FindInlineCandidates();
  size_t applied = 0;
  for (const auto &cand : candidates) {
    if (!cand.should_inline) continue;
    auto *caller = context_.FindFunction(cand.caller);
    auto *callee = context_.FindFunction(cand.callee);
    if (caller && callee) {
      inliner.InlineFunction(*caller, *callee);
      ++applied;
    }
  }
  stats_.functions_inlined += applied;
}

void GlobalOptimizer::RunDeadCodeElimination() {
  size_t before_f = CountFunctions(context_);
  size_t before_g = CountGlobals(context_);
  GlobalDeadCodeElimination pass(context_);
  pass.Run();
  size_t after_f = CountFunctions(context_);
  size_t after_g = CountGlobals(context_);
  stats_.functions_removed += before_f > after_f ? before_f - after_f : 0;
  stats_.globals_removed += before_g > after_g ? before_g - after_g : 0;
}

void GlobalOptimizer::RunConstantPropagation() {
  InterproceduralConstantPropagation pass(context_);
  pass.Run();
  stats_.constants_propagated = pass.GetConstantsPropagated();
}

void GlobalOptimizer::RunDevirtualization() {
  CrossModuleDevirtualization pass(context_);
  pass.Run();
  stats_.virtual_calls_devirtualized = pass.GetDevirtualizedCount();
}

void GlobalOptimizer::RunGlobalValueNumbering() {
  CrossModuleGVN pass(context_);
  pass.Run();
  stats_.redundant_exprs_eliminated = pass.GetEliminatedCount();
}

// ===================== LTOLinker =====================
void LTOLinker::AddInputFile(const std::string &filename) {
  input_files_.push_back(filename);
}

bool LTOLinker::Link(const std::string &output_file) {
  LTOContext context;
  config_.opt_level = opt_level_;
  auto start = Clock::now();

  if (config_.thin_lto) {
    ThinLTOCodeGenerator generator;
    for (const auto &file : input_files_) {
      std::ifstream in(file, std::ios::binary);
      if (!in) continue;
      std::stringstream buffer;
      buffer << in.rdbuf();
      generator.AddModule(file, buffer.str());
    }
    generator.OptimizeInParallel(1);
  }

  if (!LoadModules(context)) return false;
  stats_.modules_linked = context.GetModules().size();
  stats_.total_functions = CountFunctions(context);

  OptimizeModules(context);

  stats_.optimized_functions = CountFunctions(context);
  stats_.optimization_time_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

  return GenerateCode(context, output_file);
}

bool LTOLinker::LoadModules(LTOContext &context) {
  for (const auto &file : input_files_) {
    auto module = std::make_unique<LTOModule>();
    module->module_name = file;
    module->LoadBitcode(file);
    context.AddModule(std::move(module));
  }
  return true;
}

void LTOLinker::OptimizeModules(LTOContext &context) {
  if (opt_level_ <= 0) {
    context.RebuildIndexes();
    return;
  }
  
  GlobalOptimizer optimizer(context);
  
  if (config_.preserve_symbols) {
    // Run optimizations but don't eliminate functions
    optimizer.RunInlining();
    if (config_.enable_constant_prop) {
      optimizer.RunConstantPropagation();
    }
    if (config_.enable_devirtualization) {
      optimizer.RunDevirtualization();
    }
    if (config_.enable_gvn) {
      optimizer.RunGlobalValueNumbering();
    }
    context.RebuildIndexes();
  } else {
    // Run full optimization pipeline
    optimizer.Optimize();
  }
  
  // Update statistics
  auto opt_stats = optimizer.GetStatistics();
  stats_.functions_inlined = opt_stats.functions_inlined;
  stats_.functions_removed = opt_stats.functions_removed;
  stats_.globals_removed = opt_stats.globals_removed;
}

bool LTOLinker::GenerateCode(const LTOContext &context, const std::string &output) {
  // Merge all modules into a single output module and serialise as bitcode.
  // This produces a structured representation that downstream tools can
  // consume directly rather than a textual summary.
  LTOModule merged;
  merged.module_name = "lto_output";

  for (const auto &mod : context.GetModules()) {
    for (const auto &fn : mod->functions) {
      if (!merged.GetFunction(fn.name)) {
        merged.functions.push_back(fn);
      }
    }
    for (const auto &gv : mod->globals) {
      merged.globals.push_back(gv);
    }
    for (const auto &ep : mod->entry_points) {
      merged.entry_points.insert(ep);
    }
    for (const auto &[sym, vis] : mod->exported_symbols) {
      merged.exported_symbols[sym] = vis;
    }
  }

  return merged.SaveBitcode(output);
}

// ===================== ThinLTOCodeGenerator =====================

void ThinLTOCodeGenerator::AddModule(const std::string &identifier, const std::string &bitcode) {
  modules_[identifier] = bitcode;
}

bool ThinLTOCodeGenerator::Run() {
  summaries_ = GenerateSummaries();
  stats_.modules_processed = summaries_.size();
  BuildCombinedIndex();
  ComputeImportDecisions();
  return true;
}

std::vector<ThinLTOCodeGenerator::ModuleSummary> ThinLTOCodeGenerator::GenerateSummaries() const {
  std::vector<ModuleSummary> summaries;
  
  for (const auto &entry : modules_) {
    ModuleSummary summary;
    summary.module_id = entry.first;

    std::istringstream in(entry.second);
    std::string token;
    size_t functions = 0;
    size_t globals = 0;
    
    // Parse module header
    if (in >> token >> token && (in >> functions >> globals)) {
      std::string line;
      std::getline(in, line);  // consume endline
      
      // Read functions and build function summaries
      for (size_t i = 0; i < functions; ++i) {
        if (!std::getline(in, line)) break;
        if (line.empty()) {
          --i;
          continue;
        }
        
        summary.defined_symbols.push_back(line);
        
        // Create function summary
        FunctionSummary func_summary;
        func_summary.name = line;
        
        // Try to read block count for this function
        size_t block_count = 0;
        if (in >> block_count) {
          std::getline(in, line);  // consume endline
          func_summary.block_count = block_count;
          
          // Read blocks to count instructions
          for (size_t b = 0; b < block_count; ++b) {
            std::string block_name;
            size_t inst_count = 0;
            if (in >> block_name >> inst_count) {
              func_summary.instruction_count += inst_count;
            }
            std::getline(in, line);  // consume endline
          }
        }
        
        summary.functions.push_back(std::move(func_summary));
        summary.function_sizes[func_summary.name] = func_summary.instruction_count;
      }
      
      // Read globals
      for (size_t i = 0; i < globals; ++i) {
        if (!std::getline(in, line)) break;
        if (line.empty()) {
          --i;
          continue;
        }
        
        summary.referenced_symbols.push_back(line);
        
        // Create global summary
        GlobalSummary global_summary;
        global_summary.name = line;
        summary.globals.push_back(std::move(global_summary));
      }
    } else {
      // Fallback: just use module identifier as a symbol
      summary.defined_symbols.push_back(entry.first);
    }

    summaries.push_back(std::move(summary));
  }
  
  return summaries;
}

void ThinLTOCodeGenerator::BuildCombinedIndex() {
  // Build a combined index mapping symbols to their defining modules
  // This allows fast cross-module symbol resolution
  
  std::map<std::string, std::string> symbol_to_module;
  
  for (const auto& summary : summaries_) {
    for (const auto& sym : summary.defined_symbols) {
      symbol_to_module[sym] = summary.module_id;
    }
  }
  
  // Update function summaries with callee information
  // by analyzing referenced symbols
  for (auto& summary : summaries_) {
    for (auto& func : summary.functions) {
      // Add referenced symbols as potential callees
      for (const auto& ref : summary.referenced_symbols) {
        if (symbol_to_module.count(ref) && 
            symbol_to_module[ref] != summary.module_id) {
          func.callees.push_back(ref);
        }
      }
    }
  }
}

void ThinLTOCodeGenerator::ComputeImportDecisions() {
  // Determine which functions should be imported into which modules
  // based on call patterns and function sizes
  
  const size_t kImportThreshold = 50;  // Max size of functions to import
  
  std::map<std::string, size_t> function_sizes;
  std::map<std::string, std::string> function_to_module;
  
  // Collect all function sizes and module mappings
  for (const auto& summary : summaries_) {
    for (const auto& func : summary.functions) {
      function_sizes[func.name] = func.instruction_count;
      function_to_module[func.name] = summary.module_id;
    }
  }
  
  // For each function, check if any of its callees should be imported
  for (const auto& summary : summaries_) {
    for (const auto& func : summary.functions) {
      for (const auto& callee : func.callees) {
        // Check if callee is from another module
        if (function_to_module.count(callee) && 
            function_to_module[callee] != summary.module_id) {
          // Check if callee is small enough to import
          size_t callee_size = function_sizes.count(callee) ? function_sizes[callee] : 0;
          if (callee_size > 0 && callee_size <= kImportThreshold) {
            ++stats_.functions_imported;
          }
        }
      }
    }
  }
}

std::map<std::string, std::vector<std::string>> ThinLTOCodeGenerator::ComputeImports() const {
  std::map<std::string, std::vector<std::string>> imports;
  
  const size_t kImportThreshold = 50;
  
  // Build function size map
  std::map<std::string, size_t> function_sizes;
  std::map<std::string, std::string> function_to_module;
  
  for (const auto& summary : summaries_) {
    for (const auto& func : summary.functions) {
      function_sizes[func.name] = func.instruction_count;
      function_to_module[func.name] = summary.module_id;
    }
  }
  
  // Compute imports for each module
  for (const auto &summary : summaries_) {
    std::vector<std::string> module_imports;
    
    for (const auto& func : summary.functions) {
      for (const auto& callee : func.callees) {
        if (function_to_module.count(callee) && 
            function_to_module[callee] != summary.module_id) {
          size_t callee_size = function_sizes.count(callee) ? function_sizes[callee] : 0;
          if (callee_size > 0 && callee_size <= kImportThreshold) {
            module_imports.push_back(callee);
          }
        }
      }
    }
    
    // Also include referenced symbols
    for (const auto& ref : summary.referenced_symbols) {
      module_imports.push_back(ref);
    }
    
    // Remove duplicates
    std::sort(module_imports.begin(), module_imports.end());
    module_imports.erase(std::unique(module_imports.begin(), module_imports.end()), 
                         module_imports.end());
    
    imports[summary.module_id] = std::move(module_imports);
  }
  
  return imports;
}

void ThinLTOCodeGenerator::OptimizeInParallel(size_t num_threads) {
  // First run the analysis phase
  Run();
  
  if (num_threads <= 1 || summaries_.size() <= 1) {
    // Single-threaded optimization
    for (const auto& summary : summaries_) {
      // Create a temporary LTO context for this module
      LTOContext ctx;
      auto module = std::make_unique<LTOModule>();
      module->module_name = summary.module_id;
      
      // Copy function info
      for (const auto& func_summary : summary.functions) {
        ir::Function fn;
        fn.name = func_summary.name;
        module->functions.push_back(std::move(fn));
      }
      
      ctx.AddModule(std::move(module));
      
      // Run optimizer
      GlobalOptimizer optimizer(ctx);
      optimizer.Optimize();
      
      auto stats = optimizer.GetStatistics();
      stats_.functions_inlined += stats.functions_inlined;
    }
    return;
  }
  
  // Multi-threaded optimization
  std::vector<std::future<size_t>> futures;
  
  for (const auto& summary : summaries_) {
    futures.push_back(std::async(std::launch::async, [&summary]() -> size_t {
      LTOContext ctx;
      auto module = std::make_unique<LTOModule>();
      module->module_name = summary.module_id;
      
      for (const auto& func_summary : summary.functions) {
        ir::Function fn;
        fn.name = func_summary.name;
        module->functions.push_back(std::move(fn));
      }
      
      ctx.AddModule(std::move(module));
      
      GlobalOptimizer optimizer(ctx);
      optimizer.Optimize();
      
      return optimizer.GetStatistics().functions_inlined;
    }));
  }
  
  // Collect results
  for (auto& future : futures) {
    stats_.functions_inlined += future.get();
  }
}

// ===================== Utility functions =====================
bool CompileToBitcode(const std::string &source_file, const std::string &output_bitcode) {
  auto parent = std::filesystem::path(output_bitcode).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  // Load the source IR into an LTOModule, optimise, and serialise to our
  // bitcode format so downstream LTO passes operate on structured data
  // rather than raw text copies.
  LTOModule mod;
  mod.module_name = std::filesystem::path(source_file).stem().string();
  if (!mod.LoadBitcode(source_file)) {
    // If the file is not in our bitcode format yet (e.g. raw IR text),
    // attempt to parse it with the IR parser to reconstruct real functions.
    std::ifstream in(source_file, std::ios::binary);
    if (!in) return false;

    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    // Try to parse the IR text into a real IRContext.
    ir::IRContext parse_ctx;
    std::string parse_msg;
    if (ir::ParseModule(content, parse_ctx, &parse_msg)) {
      // Successfully parsed - transfer all functions to the LTOModule.
      for (auto &fn_ptr : parse_ctx.Functions()) {
        if (!fn_ptr) continue;
        mod.functions.push_back(std::move(*fn_ptr));
        mod.entry_points.insert(mod.functions.back().name);
      }
    } else {
      // Parsing failed - try to parse as individual functions.
      std::shared_ptr<ir::Function> fn_out;
      ir::IRContext single_ctx;
      if (ir::ParseFunction(content, single_ctx, &fn_out, &parse_msg) && fn_out) {
        mod.functions.push_back(std::move(*fn_out));
        mod.entry_points.insert(mod.functions.back().name);
      } else {
        // Last resort: wrap the raw text as a named function to preserve the
        // translation unit in the bitcode pipeline.
        ir::Function fn;
        fn.name = mod.module_name;
        auto block = std::make_shared<ir::BasicBlock>();
        block->name = "entry";
        auto ret = std::make_shared<ir::Instruction>();
        ret->type = ir::IRType::Void();
        ret->operands.push_back("0");
        block->SetTerminator(ret);
        fn.blocks.push_back(block);
        mod.functions.push_back(std::move(fn));
        mod.entry_points.insert(mod.module_name);
      }
    }
  }

  return mod.SaveBitcode(output_bitcode);
}

bool MergeBitcode(const std::vector<std::string> &input_files, const std::string &output_file) {
  auto parent = std::filesystem::path(output_file).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  // Merge multiple bitcode modules into a single combined module so
  // cross-module optimisation can proceed on a unified view.
  LTOModule merged;
  merged.module_name = "merged";

  for (const auto &file : input_files) {
    LTOModule mod;
    if (!mod.LoadBitcode(file)) continue;

    for (auto &fn : mod.functions) {
      // Avoid duplicating functions that already exist in the merged module.
      if (!merged.GetFunction(fn.name)) {
        merged.functions.push_back(std::move(fn));
      }
    }
    for (auto &gv : mod.globals) {
      merged.globals.push_back(std::move(gv));
    }
    for (const auto &ep : mod.entry_points) {
      merged.entry_points.insert(ep);
    }
    for (const auto &[sym, vis] : mod.exported_symbols) {
      merged.exported_symbols[sym] = vis;
    }
  }

  return merged.SaveBitcode(output_file);
}

bool GenerateObjectFromBitcode(const std::string &bitcode_file, const std::string &output_object) {
  auto parent = std::filesystem::path(output_object).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  // Load the bitcode module, run a final optimisation pass, and serialise
  // the result as an object file (our bitcode format is also the object
  // representation in the polyglot toolchain).
  LTOModule mod;
  if (!mod.LoadBitcode(bitcode_file)) return false;

  // Apply interprocedural optimisations before final emission.
  LTOContext ctx;
  auto mod_ptr = std::make_unique<LTOModule>(std::move(mod));
  ctx.AddModule(std::move(mod_ptr));
  GlobalOptimizer opt(ctx);
  opt.RunDeadCodeElimination();
  opt.RunConstantPropagation();

  // Serialise the optimised module.
  auto &modules = ctx.GetModules();
  if (modules.empty()) return false;

  return modules[0]->SaveBitcode(output_object);
}

bool LTOWorkflow::CompilePhase(const std::vector<std::string> &sources, const std::string &output_dir) {
  bool ok = true;
  if (!output_dir.empty()) {
    std::filesystem::create_directories(output_dir);
  }
  for (const auto &src : sources) {
    std::filesystem::path out = std::filesystem::path(output_dir) /
                                (std::filesystem::path(src).stem().string() + ".bc");
    ok &= CompileToBitcode(src, out.string());
  }
  return ok;
}

bool LTOWorkflow::LinkPhase(const std::vector<std::string> &bitcodes, const std::string &output_exe) {
  LTOLinker linker;
  for (const auto &bc : bitcodes) linker.AddInputFile(bc);
  linker.SetOptimizationLevel(2);
  return linker.Link(output_exe);
}

bool LTOWorkflow::FullLTO(const std::vector<std::string> &sources, const std::string &output_exe,
                          int opt_level) {
  std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "polyglot_lto";
  std::filesystem::create_directories(tmp_dir);
  std::vector<std::string> bitcodes;
  for (const auto &src : sources) {
    std::filesystem::path out = tmp_dir / (std::filesystem::path(src).stem().string() + ".bc");
    if (!CompileToBitcode(src, out.string())) return false;
    bitcodes.push_back(out.string());
  }
  LTOLinker linker;
  linker.SetOptimizationLevel(opt_level);
  for (const auto &bc : bitcodes) linker.AddInputFile(bc);
  return linker.Link(output_exe);
}

bool LTOWorkflow::ThinLTO(const std::vector<std::string> &sources, const std::string &output_exe,
                          int opt_level, size_t num_threads) {
  std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "polyglot_thin_lto";
  std::filesystem::create_directories(tmp_dir);
  
  // Step 1: Compile sources to bitcode
  std::vector<std::string> bitcodes;
  for (const auto &src : sources) {
    std::filesystem::path out = tmp_dir / (std::filesystem::path(src).stem().string() + ".bc");
    if (!CompileToBitcode(src, out.string())) return false;
    bitcodes.push_back(out.string());
  }
  
  // Step 2: Create ThinLTO generator and add modules
  ThinLTOCodeGenerator generator;
  for (const auto& bc : bitcodes) {
    std::ifstream in(bc, std::ios::binary);
    if (!in) continue;
    std::stringstream buffer;
    buffer << in.rdbuf();
    generator.AddModule(bc, buffer.str());
  }
  
  // Step 3: Optimize in parallel
  generator.OptimizeInParallel(num_threads);
  
  // Step 4: Link the optimized modules
  LTOLinker linker;
  linker.SetOptimizationLevel(opt_level);
  LTOLinker::Config config;
  config.thin_lto = true;
  config.opt_level = opt_level;
  linker.SetConfig(config);
  
  for (const auto &bc : bitcodes) {
    linker.AddInputFile(bc);
  }
  
  return linker.Link(output_exe);
}

}  // namespace polyglot::lto
