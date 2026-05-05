/**
 * @file     marshalling_view.cpp
 * @brief    Implementation of `marshalling_view.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/cross_language/marshalling_view.h"

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::cross_language {
namespace {

using Json = nlohmann::json;

MarshallingStage StageFromName(const std::string &name) {
  if (name == "helper")      return MarshallingStage::kHelper;
  if (name == "abi-adapter" || name == "abi")
    return MarshallingStage::kAbiAdapter;
  return MarshallingStage::kIRLowering;
}

void ParseSteps(const Json &arr, std::vector<MarshallingStep> &out) {
  if (!arr.is_array()) return;
  for (const auto &s : arr) {
    MarshallingStep step;
    step.stage = StageFromName(s.value("stage", std::string{"ir"}));
    step.description = s.value("description", std::string{});
    step.code_snippet = s.value("snippet", std::string{});
    if (s.contains("source") && s["source"].is_object()) {
      step.source.file = s["source"].value("file", std::string{});
      step.source.line = s["source"].value("line", 0);
      step.source.column = s["source"].value("column", 0);
    }
    out.push_back(std::move(step));
  }
}

MarshallingArgument ParseArgument(const Json &j) {
  MarshallingArgument a;
  a.name = j.value("name", std::string{});
  a.source_type = j.value("source_type", std::string{});
  a.target_type = j.value("target_type", std::string{});
  if (j.contains("steps")) ParseSteps(j["steps"], a.steps);
  return a;
}

}  // namespace

std::string MarshallingStageName(MarshallingStage s) {
  switch (s) {
    case MarshallingStage::kIRLowering: return "ir-lowering";
    case MarshallingStage::kHelper:     return "helper";
    case MarshallingStage::kAbiAdapter: return "abi-adapter";
  }
  return "unknown";
}

bool MarshallingViewBuilder::LoadAux(const std::string &json) {
  auto j = Json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  if (!j.contains("chains") || !j["chains"].is_array()) return false;
  for (const auto &c : j["chains"]) {
    MarshallingChain chain;
    chain.bridge_id = c.value("bridge_id", std::string{});
    chain.symbol = c.value("symbol", std::string{});
    if (c.contains("parameters") && c["parameters"].is_array()) {
      for (const auto &p : c["parameters"])
        chain.parameters.push_back(ParseArgument(p));
    }
    if (c.contains("return") && c["return"].is_object()) {
      chain.return_value = ParseArgument(c["return"]);
    }
    AddChain(std::move(chain));
  }
  return true;
}

void MarshallingViewBuilder::AddChain(MarshallingChain chain) {
  for (auto &c : chains_) {
    if (c.bridge_id == chain.bridge_id) { c = std::move(chain); return; }
  }
  chains_.push_back(std::move(chain));
}

const MarshallingChain *MarshallingViewBuilder::FindByBridge(
    const std::string &bridge_id) const {
  for (const auto &c : chains_)
    if (c.bridge_id == bridge_id) return &c;
  return nullptr;
}

MarshallingChain MarshallingViewBuilder::Synthesize(const Bridge &bridge) {
  MarshallingChain chain;
  chain.bridge_id = bridge.id;
  chain.symbol = bridge.stub_name.empty() ? bridge.name : bridge.stub_name;

  auto build_steps = [&](const std::string &arg_label) {
    std::vector<MarshallingStep> steps;
    MarshallingStep ir;
    ir.stage = MarshallingStage::kIRLowering;
    ir.description = "lower " + arg_label + " from " +
                     HostLanguageName(bridge.host_language) +
                     " IR to bridge IR";
    ir.code_snippet = "; ploy.bridge.lower " + arg_label;
    steps.push_back(std::move(ir));

    MarshallingStep helper;
    helper.stage = MarshallingStage::kHelper;
    helper.description = MarshallingStrategyName(bridge.strategy) +
                         " helper for " + arg_label;
    helper.code_snippet = "polyrt::marshal::" +
                          MarshallingStrategyName(bridge.strategy) + "(" +
                          arg_label + ")";
    steps.push_back(std::move(helper));

    MarshallingStep abi;
    abi.stage = MarshallingStage::kAbiAdapter;
    abi.description = HostLanguageName(bridge.target_language) +
                      " ABI adapter";
    abi.code_snippet = "// extern \"" +
                       HostLanguageName(bridge.target_language) +
                       "\" call site for " + arg_label;
    steps.push_back(std::move(abi));
    return steps;
  };

  MarshallingArgument arg;
  arg.name = "arg0";
  arg.source_type = "auto";
  arg.target_type = "auto";
  arg.steps = build_steps("arg0");
  chain.parameters.push_back(std::move(arg));

  chain.return_value.name = "ret";
  chain.return_value.source_type = "auto";
  chain.return_value.target_type = "auto";
  chain.return_value.steps = build_steps("ret");
  return chain;
}

}  // namespace polyglot::tools::ui::cross_language
