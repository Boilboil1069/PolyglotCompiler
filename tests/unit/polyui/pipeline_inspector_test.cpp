/**
 * @file     pipeline_inspector_test.cpp
 * @brief    Unit tests for `PipelineRun`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/pipeline/pipeline_inspector.h"

using namespace polyglot::tools::ui::pipeline;

TEST_CASE("PipelineStage names round-trip", "[polyui][pipeline]") {
  for (auto s : {PipelineStage::kFrontend, PipelineStage::kSema,
                 PipelineStage::kIRPreOpt, PipelineStage::kIRPostOpt,
                 PipelineStage::kBackendAsm, PipelineStage::kLink}) {
    CHECK(*PipelineStageFromName(PipelineStageName(s)) == s);
  }
  CHECK_FALSE(PipelineStageFromName("nope"));
}

TEST_CASE("LoadAux ingests every stage with artifacts and timing",
          "[polyui][pipeline]") {
  std::string aux = R"({
    "stages": [
      {"stage":"frontend","duration_ms":12,
       "artifacts":[{"label":"AST","path":"aux/ast.json","mime":"application/json"}]},
      {"stage":"sema","duration_ms":7,
       "artifacts":[{"label":"sema log","path":"aux/sema.txt"}]},
      {"stage":"ir-pre-opt","duration_ms":18,
       "artifacts":[{"label":"IR","path":"aux/pre.ll"}]},
      {"stage":"ir-post-opt","duration_ms":24,
       "artifacts":[{"label":"IR","path":"aux/post.ll"}]},
      {"stage":"backend-asm","duration_ms":40,
       "artifacts":[{"label":"asm","path":"aux/out.s"}]},
      {"stage":"link","duration_ms":11,
       "artifacts":[{"label":"map","path":"aux/link.map"}]}
    ]
  })";
  PipelineRun r;
  REQUIRE(r.LoadAux(aux));
  CHECK(r.stages().size() == 6);
  CHECK(r.TotalDuration().count() == 12 + 7 + 18 + 24 + 40 + 11);
  const auto *post = r.Find(PipelineStage::kIRPostOpt);
  REQUIRE(post);
  REQUIRE(post->artifacts.size() == 1);
  CHECK(post->artifacts[0].path == "aux/post.ll");
}

TEST_CASE("Histogram normalises against the longest stage",
          "[polyui][pipeline]") {
  PipelineRun r;
  StageRecord a; a.stage = PipelineStage::kFrontend;
  a.duration = std::chrono::milliseconds(50);
  StageRecord b; b.stage = PipelineStage::kBackendAsm;
  b.duration = std::chrono::milliseconds(200);
  r.AddStage(a); r.AddStage(b);
  auto h = r.Histogram();
  REQUIRE(h.size() == 2);
  CHECK(h[0].second == 0.25);
  CHECK(h[1].second == 1.0);
}
