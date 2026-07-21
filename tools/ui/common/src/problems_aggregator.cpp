/**
 * @file     problems_aggregator.cpp
 * @brief    Implementation of @ref polyglot::tools::ui::ProblemsAggregator
 * @ingroup  Tool / polyui / problems
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/include/problems_aggregator.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace polyglot::tools::ui {

// ============================================================================
// Severity helpers
// ============================================================================

Severity ClassifySeverity(const std::string &label) {
  // Case-insensitive comparison.
  std::string lower(label.size(), '\0');
  std::transform(label.begin(), label.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "warning" || lower == "warn") return Severity::kWarning;
  if (lower == "information" || lower == "info") return Severity::kInformation;
  if (lower == "hint" || lower == "note") return Severity::kHint;
  return Severity::kError;
}

namespace {

inline std::uint32_t SeverityBit(Severity s) {
  switch (s) {
    case Severity::kError:       return static_cast<std::uint32_t>(SeverityMask::kError);
    case Severity::kWarning:     return static_cast<std::uint32_t>(SeverityMask::kWarning);
    case Severity::kInformation: return static_cast<std::uint32_t>(SeverityMask::kInformation);
    case Severity::kHint:        return static_cast<std::uint32_t>(SeverityMask::kHint);
  }
  return 0;
}

}  // namespace

// ============================================================================
// Mutators
// ============================================================================

void ProblemsAggregator::Replace(const std::string &file, const std::string &source,
                                 std::vector<ProblemEntry> entries) {
  ChangeCallback cb;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (entries.empty()) {
      auto it = slices_.find(file);
      if (it != slices_.end()) {
        it->second.erase(source);
        if (it->second.empty()) {
          slices_.erase(it);
        }
      }
    } else {
      // Make sure stored entries carry the (file, source) coordinates so
      // a downstream consumer never has to re-derive them.
      for (auto &e : entries) {
        e.file = file;
        e.source = source;
      }
      slices_[file][source] = std::move(entries);
    }
    cb = on_changed_;
  }
  if (cb) cb();
}

void ProblemsAggregator::ReplaceFromDiagnosticInfo(
    const std::string &file, const std::string &source,
    const std::vector<DiagnosticInfo> &diags) {
  std::vector<ProblemEntry> entries;
  entries.reserve(diags.size());
  for (const auto &d : diags) {
    ProblemEntry e;
    e.severity = ClassifySeverity(d.severity);
    e.line = d.line;
    e.column = d.column;
    e.end_line = d.end_line ? d.end_line : d.line;
    e.end_column = d.end_column ? d.end_column : d.column;
    e.code = d.code;
    e.message = d.message;
    e.suggestion = d.suggestion;
    entries.push_back(std::move(e));
  }
  Replace(file, source, std::move(entries));
}

void ProblemsAggregator::ClearSource(const std::string &source) {
  ChangeCallback cb;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto it = slices_.begin(); it != slices_.end();) {
      it->second.erase(source);
      if (it->second.empty()) {
        it = slices_.erase(it);
      } else {
        ++it;
      }
    }
    cb = on_changed_;
  }
  if (cb) cb();
}

void ProblemsAggregator::ClearFile(const std::string &file) {
  ChangeCallback cb;
  {
    std::lock_guard<std::mutex> lock(mu_);
    slices_.erase(file);
    cb = on_changed_;
  }
  if (cb) cb();
}

void ProblemsAggregator::Clear() {
  ChangeCallback cb;
  {
    std::lock_guard<std::mutex> lock(mu_);
    slices_.clear();
    cb = on_changed_;
  }
  if (cb) cb();
}

void ProblemsAggregator::SetChangeCallback(ChangeCallback cb) {
  std::lock_guard<std::mutex> lock(mu_);
  on_changed_ = std::move(cb);
}

// ============================================================================
// Read-side
// ============================================================================

bool ProblemsAggregator::ContainsCaseInsensitive(const std::string &haystack,
                                                 const std::string &needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  auto to_lower = [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  };
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool ok = true;
    for (std::size_t k = 0; k < needle.size(); ++k) {
      if (to_lower(static_cast<unsigned char>(haystack[i + k])) !=
          to_lower(static_cast<unsigned char>(needle[k]))) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

bool ProblemsAggregator::MatchesFilter(const ProblemEntry &e, const ProblemFilter &f,
                                       const std::regex *compiled_regex) {
  if ((SeverityBit(e.severity) & f.severity_mask) == 0) return false;
  if (!ContainsCaseInsensitive(e.file, f.file_substring)) return false;
  if (!ContainsCaseInsensitive(e.source, f.source_substring)) return false;
  if (compiled_regex) {
    if (!std::regex_search(e.message, *compiled_regex)) return false;
  }
  return true;
}

std::vector<ProblemEntry> ProblemsAggregator::Snapshot(const ProblemFilter &filter) const {
  // Compile the regex once; an invalid pattern is treated as "no regex",
  // which matches the Problems Panel's lenient UX.
  std::unique_ptr<std::regex> compiled;
  if (!filter.message_regex.empty()) {
    try {
      compiled = std::make_unique<std::regex>(filter.message_regex,
                                              std::regex::ECMAScript | std::regex::icase);
    } catch (const std::regex_error &) {
      compiled.reset();
    }
  }

  std::vector<ProblemEntry> out;
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto &[file, by_source] : slices_) {
    for (const auto &[source, entries] : by_source) {
      for (const auto &e : entries) {
        if (MatchesFilter(e, filter, compiled.get())) {
          out.push_back(e);
        }
      }
    }
  }
  // Stable sort by (file, line, column, severity).  Within a file we
  // surface errors first when two entries share the same coordinates.
  std::sort(out.begin(), out.end(), [](const ProblemEntry &a, const ProblemEntry &b) {
    if (a.file != b.file) return a.file < b.file;
    if (a.line != b.line) return a.line < b.line;
    if (a.column != b.column) return a.column < b.column;
    return static_cast<int>(a.severity) < static_cast<int>(b.severity);
  });
  return out;
}

ProblemsAggregator::Counts ProblemsAggregator::CountAll() const {
  Counts c;
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto &[file, by_source] : slices_) {
    (void)file;
    for (const auto &[source, entries] : by_source) {
      (void)source;
      for (const auto &e : entries) {
        switch (e.severity) {
          case Severity::kError:       ++c.errors; break;
          case Severity::kWarning:     ++c.warnings; break;
          case Severity::kInformation: ++c.information; break;
          case Severity::kHint:        ++c.hints; break;
        }
      }
    }
  }
  return c;
}

std::vector<std::string> ProblemsAggregator::KnownSources() const {
  std::set<std::string> uniq;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto &[file, by_source] : slices_) {
      (void)file;
      for (const auto &[source, entries] : by_source) {
        (void)entries;
        uniq.insert(source);
      }
    }
  }
  return std::vector<std::string>(uniq.begin(), uniq.end());
}

void ProblemsAggregator::NotifyLocked() {
  if (on_changed_) on_changed_();
}

}  // namespace polyglot::tools::ui
