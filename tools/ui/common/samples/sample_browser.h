/**
 * @file     sample_browser.h
 * @brief    Sample / Tutorial browser value model.
 *
 * The Sample / Tutorial browser indexes the in-tree
 * `tests/samples/` and `docs/tutorial/` trees, exposes them as a
 * filterable catalogue (by language / topic / difficulty) and
 * provides "open as workspace copy" semantics: a request to clone
 * an entry into a destination directory, returning the planned
 * file copies so the IDE can perform them transactionally without
 * touching the source tree.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::samples {

enum class EntryKind {
  kSample,
  kTutorial,
};

enum class Difficulty {
  kBeginner,
  kIntermediate,
  kAdvanced,
};

std::string EntryKindName(EntryKind k);
std::optional<EntryKind> EntryKindFromName(const std::string &name);
std::string DifficultyName(Difficulty d);
std::optional<Difficulty> DifficultyFromName(const std::string &name);

struct CatalogueEntry {
  std::string id;
  std::string title;
  EntryKind kind{EntryKind::kSample};
  Difficulty difficulty{Difficulty::kBeginner};
  std::vector<std::string> languages;   ///< e.g. ["cpp","python"].
  std::vector<std::string> topics;      ///< Free-form taxonomy.
  std::string root_path;                ///< Source directory (relative).
  std::vector<std::string> files;       ///< Files (relative to root_path).
  std::string summary;
};

struct CatalogueQuery {
  std::vector<std::string> languages;
  std::vector<std::string> topics;
  std::optional<Difficulty> difficulty;
  std::optional<EntryKind> kind;
  std::string text;                     ///< Free-text fragment.
};

struct CopyPlan {
  std::string entry_id;
  std::string destination_root;
  std::vector<std::pair<std::string, std::string>> files;  ///< (src, dst).
};

class SampleCatalogue {
 public:
  void AddEntry(CatalogueEntry entry);

  /// Load a catalogue document (samples + tutorials) from JSON.
  bool LoadIndex(const std::string &json);

  const std::vector<CatalogueEntry> &entries() const { return entries_; }
  const CatalogueEntry *Find(const std::string &id) const;

  /// Return entries matching every populated filter in `q`.
  std::vector<const CatalogueEntry *> Filter(const CatalogueQuery &q) const;

  /// Plan a "open as workspace copy" of `entry_id` into
  /// `destination_root`.  Returns std::nullopt if the entry is
  /// unknown.
  std::optional<CopyPlan> PlanCopy(const std::string &entry_id,
                                   const std::string &destination_root) const;

 private:
  std::vector<CatalogueEntry> entries_;
};

}  // namespace polyglot::tools::ui::samples
