/**
 * @file     refactor_diff.h
 * @brief    Per-hunk accept/reject model for refactor proposals.
 *
 * Wraps the `RefactorHunk` list returned by an `AiProvider` and
 * tracks accept / reject state so the UI can show a diff and
 * compute the final patch once the user is done.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <string>
#include <vector>

#include "tools/ui/common/ai/ai_provider.h"

namespace polyglot::tools::ui::ai {

enum class HunkDecision {
  kPending,
  kAccepted,
  kRejected,
};

std::string HunkDecisionName(HunkDecision d);

struct RefactorHunkView {
  RefactorHunk hunk;
  HunkDecision decision{HunkDecision::kPending};
};

class RefactorReviewSession {
 public:
  void Load(RefactorSuggestResponse response);

  size_t size() const { return views_.size(); }
  const std::vector<RefactorHunkView> &views() const { return views_; }
  const std::string &summary() const { return summary_; }

  bool Accept(size_t index);
  bool Reject(size_t index);
  void AcceptAll();
  void RejectAll();

  size_t accepted_count() const;
  size_t rejected_count() const;
  size_t pending_count() const;

  /// Hunks the user has accepted so far, in original order.
  std::vector<RefactorHunk> AcceptedHunks() const;

  /// Render a unified diff for the accepted hunks suitable for
  /// `patch -p0`.  Each hunk is emitted as a separate file header
  /// + `@@` block.
  std::string RenderUnifiedDiff() const;

 private:
  std::vector<RefactorHunkView> views_;
  std::string summary_;
};

}  // namespace polyglot::tools::ui::ai
