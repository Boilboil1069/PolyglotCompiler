/**
 * @file     cross_language_navigator.h
 * @brief    Goto-def, reverse references and rename across LSPs.
 *
 * `.poly` carries `LINK <lang>::<symbol>` references that resolve
 * to host-language definitions (C++, Rust, Python, Java, .NET).
 * The navigator owns a catalogue of these link sites alongside the
 * resolved definitions, and answers three questions on the IDE's
 * behalf:
 *
 *   * **Goto definition** — given a `.poly` link site, locate the
 *     host-language file/line that owns the target symbol.
 *   * **Reverse references** — for a host-language definition,
 *     enumerate every `.poly` site that links to it (used by the
 *     "X `.poly` LINK references" CodeLens).
 *   * **Coordinated rename** — produce a single `WorkspaceEdit`
 *     plan that touches both `.poly` link sites and host-language
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
  kGo,
  kJavaScript,
  kRuby,
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
  SourceLocation location;   ///< Position in the `.poly` file.
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

  /// All `.poly` LINK sites pointing at `definition`.
  std::vector<LinkSite> FindLinkReferences(const Definition &def) const;

  /// CodeLens entries that should appear above each definition in
  /// `file`; reports the count of `.poly` LINK references per
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
/// to `new_name`.  The plan covers every `.poly` link site and
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


/// Source-backed documentation. Positions are one-based; no generated prose is
/// substituted when the source does not contain documentation.
struct FunctionDocumentation {
  std::string name;
  std::string language;
  std::string signature;
  std::string documentation;
  std::string source_preview;
  SourceLocation location;
  // ABI types come from the same foreign signature extractor as polyc;
  // source comments above remain the original author's documentation.
  std::string compiler_signature;
  std::string type_resolution_note;
};

struct ForeignCallTarget {
  std::string language;
  std::string qualified_symbol;
};

/// Lightweight source outline, including adjacent source comments / docstrings.
/// This is an editor index, not a language type checker. Overloads remain
/// separate results so callers can present an explicit candidate picker.
std::vector<FunctionDocumentation> ExtractFunctionDocumentation(
    const std::string &source, const std::string &language,
    const std::string &filename);

/// Resolve CALL(lang, module::function, ...) or LINK lang::module::function
/// under a zero-based byte offset. Ignores comments and strings.
std::optional<ForeignCallTarget> ForeignTargetAt(
    const std::string &poly_source, std::size_t offset);

/// Candidate source paths for the local module convention used by polyc:
/// module.ext and <language>/module.ext, relative to the Poly source.
std::vector<std::string> ForeignSourceCandidates(
    const std::string &poly_file, const ForeignCallTarget &target);

}  // namespace polyglot::tools::ui::cross_language
