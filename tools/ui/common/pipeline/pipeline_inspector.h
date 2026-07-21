/**
 * @file     pipeline_inspector.h
 * @brief    Compile pipeline inspector value model.
 *
 * The Pipeline Inspector renders every stage of a compilation
 * (frontend, sema, IR pre-opt, IR post-opt, backend asm, link)
 * with the artefacts polyc dropped under `aux/` plus a duration
 * histogram across stages.  Selecting a stage exposes the artefact
 * paths so the IDE can hand them off to the IR or Asm viewer.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::pipeline {

enum class PipelineStage {
  kFrontend,
  kSema,
  kIRPreOpt,
  kIRPostOpt,
  kBackendAsm,
  kLink,
};

std::string PipelineStageName(PipelineStage s);
std::optional<PipelineStage> PipelineStageFromName(const std::string &name);

struct Artifact {
  std::string label;         ///< Human-readable artefact name.
  std::string path;          ///< Workspace-relative path under `aux/`.
  std::string mime;          ///< "text/plain", "text/asm", etc.
};

struct StageRecord {
  PipelineStage stage{PipelineStage::kFrontend};
  std::chrono::milliseconds duration{0};
  std::vector<Artifact> artifacts;
};

class PipelineRun {
 public:
  void AddStage(StageRecord record);

  /// Load the per-stage records from polyc's `aux/pipeline.json`.
  bool LoadAux(const std::string &json);

  const std::vector<StageRecord> &stages() const { return stages_; }
  const StageRecord *Find(PipelineStage stage) const;

  /// Total wall-clock time across every recorded stage.
  std::chrono::milliseconds TotalDuration() const;

  /// Histogram bars normalised against the longest stage.  Each
  /// entry is `(stage, ratio in [0,1])`.
  std::vector<std::pair<PipelineStage, double>> Histogram() const;

 private:
  std::vector<StageRecord> stages_;
};

}  // namespace polyglot::tools::ui::pipeline
