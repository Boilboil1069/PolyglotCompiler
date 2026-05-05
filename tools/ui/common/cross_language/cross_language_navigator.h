/**
 * @file     cross_language_navigator.h
 * @brief    Goto-def, reverse references and rename across LSPs.
 *
 * `.ploy` carries `LINK <lang>::<symbol>` references that resolve
 * to host-language definitions (C++, Rust, Python, Java, .NET).
 * The navigator owns a catalogue of these link sites alongside the
 * resolved definitions, and answers three questions on the IDE's
 * behalf:
 *
 *   * **Goto definition** — given a `.ploy` link site, locate the
 *     host-language file/line that owns the target symbol.
 *   * **Reverse references** — for a host-language definition,
 *     enumerate every `.ploy` site that links to it (used by the
 *     "X `.ploy` LINK references" CodeLens).
 *   * **Coordinated rename** — produce a single `WorkspaceEdit`
 *     plan that touches both `.ploy` link sites and host-language
 *     definitions/references in lockstep, so polyls can submit it
 *     atomically across the underlying LSPs.
 *
 * The navigator is purely a value model; the LSP transport that
 * invokes the per-language servers is supplied by polyls.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::cross_language {

enum class HostLanguage {
  kCpp,
  kRust,
  kPython,
  kJava,
  kDotnet,
};

std::string HostLanguageName(HostLanguage l);
std::optional<HostLanguage> HostLanguageFromName(const std::string &name);

struct SourceLocation {
  std::string file;
  int line{0};
  int column{0};
};

struct LinkSite {
  std::string id;            ///< Stable id for the catalogue.
  SourceLocation location;   ///< Position in the `.ploy` file.
  HostLanguage target_language{HostLanguage::kCpp};
  std::string target_symbol; ///< e.g. "math::add" or "pkg.module.fn".
};

struct Definition {
  HostLanguage language{HostLanguage::kCpp};
  SourceLocation location;
  std::string symbol;
};

struct Reference {
  SourceLocation location;
  std::string symbol;
};

struct WorkspaceEdit {
  std::string file;
  int line{0};
  int column{0};
  int length{0};
  std::string new_text;
};

class LinkRegistry {
 public:
  void AddSite(LinkSite site);
  void AddDefinition(Definition def);

  /// Resolve `site.target_symbol` to its host-language definition.
  std::optional<Definition> GotoDefinition(const LinkSite &site) const;

  /// All `.ploy` LINK sites pointing at `definition`.
  std::vector<LinkSite> FindLinkReferences(const Definition &def) const;

  /// CodeLens entries that should appear above each definition in
  /// `file`; reports the count of `.ploy` LINK references per
  /// definition.
  struct CodeLens {
    SourceLocation anchor;
    std::string symbol;
    int reference_count{0};
  };
  std::vector<CodeLens> CodeLensFor(const std::string &host_file) const;

  const std::vector<LinkSite> &sites() const { return sites_; }
  const std::vector<Definition> &definitions() const { return defs_; }

 private:
  std::vector<LinkSite> sites_;
  std::vector<Definition> defs_;
};

/// Build a coordinated `WorkspaceEdit` plan for renaming `symbol`
/// to `new_name`.  The plan covers every `.ploy` link site and
/// every host-language definition/reference recorded in the
/// registry plus the supplied `extra_references`.
class RenamePlanner {
 public:
  explicit RenamePlanner(const LinkRegistry &registry)
      : registry_(registry) {}

  std::vector<WorkspaceEdit> Plan(
      HostLanguage language,
      const std::string &symbol,
      const std::string &new_name,
      const std::vector<Reference> &extra_references = {}) const;

 private:
  const LinkRegistry &registry_;
};

}  // namespace polyglot::tools::ui::cross_language
