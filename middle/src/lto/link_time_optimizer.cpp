#include "middle/include/lto/link_time_optimizer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace polyglot::lto {

using Clock = std::chrono::steady_clock;

// ===================== LTOModule =====================
bool LTOModule::SaveBitcode(const std::string &filename) const {
  std::ofstream out(filename, std::ios::binary);
  if (!out) return false;

  out << "module " << module_name << "\n";
  out << functions.size() << ' ' << globals.size() << "\n";
  for (const auto &fn : functions) {
    out << fn.name << "\n";
  }
  for (const auto &gv : globals) {
    out << gv.name << "\n";
  }

  return static_cast<bool>(out);
}

bool LTOModule::LoadBitcode(const std::string &filename) {
  std::ifstream in(filename, std::ios::binary);
  if (!in) return false;

  functions.clear();
  globals.clear();

  std::string header;
  if (!(in >> header >> module_name)) return false;
  size_t fn_count = 0, gv_count = 0;
  if (!(in >> fn_count >> gv_count)) return false;
  std::string line;
  std::getline(in, line);  // consume endline

  for (size_t i = 0; i < fn_count; ++i) {
    if (!std::getline(in, line)) break;
    if (line.empty()) {
      --i;
      continue;
    }
    ir::Function fn;
    fn.name = line;
    functions.push_back(std::move(fn));
  }

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

  return true;
}

// ===================== LTOContext =====================
void LTOContext::AddModule(std::unique_ptr<LTOModule> module) {
  modules_.push_back(std::move(module));
  auto *stored = modules_.back().get();
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

ir::GlobalValue *LTOContext::FindGlobal(const std::string &name) {
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

LTOContext::CallGraph LTOContext::BuildCallGraph() const {
  CallGraph graph;
  for (const auto &mod : modules_) {
    for (const auto &fn : mod->functions) {
      auto &node = graph.nodes[fn.name];
      node.function_name = fn.name;
    }
  }
  // With no IR-level call extraction, return a graph of isolated nodes.
  return graph;
}

std::vector<std::string> LTOContext::CallGraph::GetRoots() const {
  std::vector<std::string> roots;
  for (const auto &[name, node] : nodes) {
    if (node.callers.empty()) roots.push_back(name);
  }
  return roots;
}

std::vector<std::string> LTOContext::CallGraph::GetLeaves() const {
  std::vector<std::string> leaves;
  for (const auto &[name, node] : nodes) {
    if (node.callees.empty()) leaves.push_back(name);
  }
  return leaves;
}

// ===================== CrossModuleInliner =====================
void CrossModuleInliner::Run() {
  auto candidates = FindInlineCandidates();
  for (const auto &cand : candidates) {
    if (!cand.should_inline) continue;
    auto *caller = context_.FindFunction(cand.caller);
    auto *callee = context_.FindFunction(cand.callee);
    if (caller && callee) {
      InlineFunction(*caller, *callee);
    }
  }
}

std::vector<CrossModuleInliner::InlineCandidate> CrossModuleInliner::FindInlineCandidates() const {
  std::vector<InlineCandidate> result;
  auto cg = context_.BuildCallGraph();
  for (const auto &[fn_name, node] : cg.nodes) {
    for (const auto &callee : node.callees) {
      InlineCandidate cand{fn_name, callee, node.callees.size(), 0, false};
      auto *caller_fn = context_.FindFunction(fn_name);
      auto *callee_fn = context_.FindFunction(callee);
      if (caller_fn && callee_fn) {
        cand.should_inline = ShouldInline(*caller_fn, *callee_fn);
      }
      result.push_back(cand);
    }
  }
  return result;
}

bool CrossModuleInliner::ShouldInline(const ir::Function &caller, const ir::Function &callee) const {
  (void)caller;
  // Heuristic: inline tiny functions (placeholder: name length based).
  return callee.name.size() < 32;
}

void CrossModuleInliner::InlineFunction(ir::Function &caller, const ir::Function &callee) {
  // Simplified inline: clone callee blocks onto caller for illustration.
  for (const auto &block : callee.blocks) {
    if (!block) continue;
    auto cloned = std::make_shared<ir::BasicBlock>(*block);
    cloned->name = caller.name + "_inl_" + block->name;
    caller.blocks.push_back(std::move(cloned));
  }
  (void)caller;
}

// ===================== GlobalDeadCodeElimination =====================
void GlobalDeadCodeElimination::Run() {
  auto reachable = MarkReachableSymbols();
  for (auto &mod : context_.MutableModules()) {
    auto &funcs = mod->functions;
    funcs.erase(std::remove_if(funcs.begin(), funcs.end(), [&](const ir::Function &fn) {
                  return !reachable.empty() && reachable.count(fn.name) == 0;
                }),
                funcs.end());

    auto &globals = mod->globals;
    globals.erase(std::remove_if(globals.begin(), globals.end(), [&](const ir::GlobalValue &gv) {
                    return !reachable.empty() && reachable.count(gv.name) == 0;
                  }),
                  globals.end());
  }
  context_.RebuildIndexes();
}

std::set<std::string> GlobalDeadCodeElimination::MarkReachableSymbols() const {
  std::set<std::string> reachable;
  for (const auto &mod : context_.GetModules()) {
    for (const auto &fn : mod->functions) reachable.insert(fn.name);
    for (const auto &gv : mod->globals) reachable.insert(gv.name);
    for (const auto &[name, is_public] : mod->exported_symbols) {
      if (is_public) reachable.insert(name);
    }
    for (const auto &dep : mod->dependencies) reachable.insert(dep);
  }
  return reachable;
}

// ===================== InterproceduralConstantPropagation =====================
void InterproceduralConstantPropagation::Run() {
  (void)AnalyzeConstantArgs();
}

std::map<std::string, std::vector<ir::Value>> InterproceduralConstantPropagation::AnalyzeConstantArgs() const {
  return {};
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
}

void GlobalOptimizer::RunDevirtualization() {
  // Placeholder: no devirtualization yet.
  stats_.virtual_calls_devirtualized = 0;
}

void GlobalOptimizer::RunGlobalValueNumbering() {
  // Placeholder GVN stage.
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
    optimizer.RunInlining();
    optimizer.RunConstantPropagation();
    optimizer.RunDevirtualization();
    optimizer.RunGlobalValueNumbering();
    context.RebuildIndexes();
    return;
  }
  optimizer.Optimize();
}

bool LTOLinker::GenerateCode(const LTOContext &context, const std::string &output) {
  std::ofstream out(output, std::ios::binary);
  if (!out) return false;

  out << "LTO output for " << context.GetModules().size() << " modules\n";
  out << "Functions: " << CountFunctions(context) << "\n";
  out << "Globals: " << CountGlobals(context) << "\n";

  return static_cast<bool>(out);
}

// ===================== ThinLTOCodeGenerator =====================
void ThinLTOCodeGenerator::AddModule(const std::string &identifier, const std::string &bitcode) {
  modules_[identifier] = bitcode;
}

bool ThinLTOCodeGenerator::Run() {
  summaries_ = GenerateSummaries();
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
    if (in >> token >> token && (in >> functions >> globals)) {
      std::string line;
      std::getline(in, line);  // consume endline
      for (size_t i = 0; i < functions; ++i) {
        if (!std::getline(in, line)) break;
        if (!line.empty()) summary.defined_symbols.push_back(line);
      }
      for (size_t i = 0; i < globals; ++i) {
        if (!std::getline(in, line)) break;
        if (!line.empty()) summary.referenced_symbols.push_back(line);
      }
    } else {
      summary.defined_symbols.push_back(entry.first);
    }

    summaries.push_back(std::move(summary));
  }
  return summaries;
}

std::map<std::string, std::vector<std::string>> ThinLTOCodeGenerator::ComputeImports() const {
  std::map<std::string, std::vector<std::string>> imports;
  for (const auto &summary : summaries_) {
    imports[summary.module_id] = summary.referenced_symbols;
  }
  return imports;
}

void ThinLTOCodeGenerator::OptimizeInParallel(size_t num_threads) {
  (void)num_threads;
  Run();
}

// ===================== Utility functions =====================
bool CompileToBitcode(const std::string &source_file, const std::string &output_bitcode) {
  std::ifstream in(source_file, std::ios::binary);
  if (!in) return false;
  auto parent = std::filesystem::path(output_bitcode).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream out(output_bitcode, std::ios::binary);
  if (!out) return false;
  out << in.rdbuf();
  return static_cast<bool>(out);
}

bool MergeBitcode(const std::vector<std::string> &input_files, const std::string &output_file) {
  auto parent = std::filesystem::path(output_file).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream out(output_file, std::ios::binary);
  if (!out) return false;
  for (const auto &file : input_files) {
    std::ifstream in(file, std::ios::binary);
    if (!in) continue;
    out << in.rdbuf();
  }
  return static_cast<bool>(out);
}

bool GenerateObjectFromBitcode(const std::string &bitcode_file, const std::string &output_object) {
  return CompileToBitcode(bitcode_file, output_object);
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

}  // namespace polyglot::lto
