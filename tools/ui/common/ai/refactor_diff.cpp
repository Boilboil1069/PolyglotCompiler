/**
 * @file     refactor_diff.cpp
 * @brief    Implementation of `RefactorReviewSession`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/ai/refactor_diff.h"

#include <sstream>

namespace polyglot::tools::ui::ai {

std::string HunkDecisionName(HunkDecision d) {
  switch (d) {
    case HunkDecision::kPending:  return "pending";
    case HunkDecision::kAccepted: return "accepted";
    case HunkDecision::kRejected: return "rejected";
  }
  return "pending";
}

void RefactorReviewSession::Load(RefactorSuggestResponse response) {
  summary_ = std::move(response.summary);
  views_.clear();
  views_.reserve(response.hunks.size());
  for (auto &h : response.hunks) {
    views_.push_back({std::move(h), HunkDecision::kPending});
  }
}

bool RefactorReviewSession::Accept(size_t i) {
  if (i >= views_.size()) return false;
  views_[i].decision = HunkDecision::kAccepted;
  return true;
}

bool RefactorReviewSession::Reject(size_t i) {
  if (i >= views_.size()) return false;
  views_[i].decision = HunkDecision::kRejected;
  return true;
}

void RefactorReviewSession::AcceptAll() {
  for (auto &v : views_) v.decision = HunkDecision::kAccepted;
}

void RefactorReviewSession::RejectAll() {
  for (auto &v : views_) v.decision = HunkDecision::kRejected;
}

size_t RefactorReviewSession::accepted_count() const {
  size_t n = 0;
  for (const auto &v : views_)
    if (v.decision == HunkDecision::kAccepted) ++n;
  return n;
}

size_t RefactorReviewSession::rejected_count() const {
  size_t n = 0;
  for (const auto &v : views_)
    if (v.decision == HunkDecision::kRejected) ++n;
  return n;
}

size_t RefactorReviewSession::pending_count() const {
  size_t n = 0;
  for (const auto &v : views_)
    if (v.decision == HunkDecision::kPending) ++n;
  return n;
}

std::vector<RefactorHunk> RefactorReviewSession::AcceptedHunks() const {
  std::vector<RefactorHunk> out;
  for (const auto &v : views_)
    if (v.decision == HunkDecision::kAccepted) out.push_back(v.hunk);
  return out;
}

namespace {

int CountLines(const std::string &s) {
  if (s.empty()) return 0;
  int n = 1;
  for (char c : s)
    if (c == '\n') ++n;
  // A trailing newline does not add a logical line.
  if (s.back() == '\n') --n;
  return n == 0 ? 1 : n;
}

void EmitBlock(std::ostringstream &oss, char prefix,
               const std::string &text) {
  std::string line;
  for (char c : text) {
    if (c == '\n') {
      oss << prefix << line << '\n';
      line.clear();
    } else {
      line.push_back(c);
    }
  }
  if (!line.empty()) oss << prefix << line << '\n';
}

}  // namespace

std::string RefactorReviewSession::RenderUnifiedDiff() const {
  std::ostringstream oss;
  for (const auto &v : views_) {
    if (v.decision != HunkDecision::kAccepted) continue;
    const auto &h = v.hunk;
    oss << "--- " << h.file_path << "\n";
    oss << "+++ " << h.file_path << "\n";
    int old_len = CountLines(h.original);
    int new_len = CountLines(h.replacement);
    int start = h.start_line > 0 ? h.start_line : 1;
    oss << "@@ -" << start << "," << old_len << " +" << start << ","
        << new_len << " @@\n";
    EmitBlock(oss, '-', h.original);
    EmitBlock(oss, '+', h.replacement);
  }
  return oss.str();
}

}  // namespace polyglot::tools::ui::ai
