/**
 * @file     pipeline_inspector.cpp
 * @brief    Implementation of `pipeline_inspector.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/pipeline/pipeline_inspector.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::pipeline {
namespace {

using Json = nlohmann::json;

}  // namespace

std::string PipelineStageName(PipelineStage s) {
  switch (s) {
    case PipelineStage::kFrontend:    return "frontend";
    case PipelineStage::kSema:        return "sema";
    case PipelineStage::kIRPreOpt:    return "ir-pre-opt";
    case PipelineStage::kIRPostOpt:   return "ir-post-opt";
    case PipelineStage::kBackendAsm:  return "backend-asm";
    case PipelineStage::kLink:        return "link";
  }
  return "unknown";
}

std::optional<PipelineStage> PipelineStageFromName(const std::string &name) {
  if (name == "frontend")    return PipelineStage::kFrontend;
  if (name == "sema")        return PipelineStage::kSema;
  if (name == "ir-pre-opt")  return PipelineStage::kIRPreOpt;
  if (name == "ir-post-opt") return PipelineStage::kIRPostOpt;
  if (name == "backend-asm") return PipelineStage::kBackendAsm;
  if (name == "link")        return PipelineStage::kLink;
  return std::nullopt;
}

void PipelineRun::AddStage(StageRecord record) {
  for (auto &existing : stages_) {
    if (existing.stage == record.stage) {
      existing = std::move(record);
      return;
    }
  }
  stages_.push_back(std::move(record));
}

bool PipelineRun::LoadAux(const std::string &json) {
  auto j = Json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  if (!j.contains("stages") || !j["stages"].is_array()) return false;
  for (const auto &s : j["stages"]) {
    StageRecord rec;
    auto stage = PipelineStageFromName(s.value("stage", std::string{}));
    if (!stage) continue;
    rec.stage = *stage;
    rec.duration = std::chrono::milliseconds(s.value("duration_ms", 0LL));
    if (s.contains("artifacts") && s["artifacts"].is_array()) {
      for (const auto &a : s["artifacts"]) {
        Artifact art;
        art.label = a.value("label", std::string{});
        art.path = a.value("path", std::string{});
        art.mime = a.value("mime", std::string{"text/plain"});
        rec.artifacts.push_back(std::move(art));
      }
    }
    AddStage(std::move(rec));
  }
  return true;
}

const StageRecord *PipelineRun::Find(PipelineStage stage) const {
  for (const auto &s : stages_)
    if (s.stage == stage) return &s;
  return nullptr;
}

std::chrono::milliseconds PipelineRun::TotalDuration() const {
  std::chrono::milliseconds total{0};
  for (const auto &s : stages_) total += s.duration;
  return total;
}

std::vector<std::pair<PipelineStage, double>> PipelineRun::Histogram() const {
  std::vector<std::pair<PipelineStage, double>> out;
  long long max_ms = 0;
  for (const auto &s : stages_)
    max_ms = std::max(max_ms, s.duration.count());
  for (const auto &s : stages_) {
    double ratio = max_ms > 0
        ? static_cast<double>(s.duration.count()) / max_ms
        : 0.0;
    out.emplace_back(s.stage, ratio);
  }
  return out;
}

}  // namespace polyglot::tools::ui::pipeline
