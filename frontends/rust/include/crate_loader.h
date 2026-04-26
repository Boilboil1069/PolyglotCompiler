/**
 * @file     crate_loader.h
 * @brief    Rust crate metadata loader (cargo + source-walking + rmeta probe).
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-27
 *
 * Resolves `--crate-dir=<dir>` and `--extern <name>=<path>` driver options
 * into a queryable index of public items.  For source crates the loader
 * reuses the existing Rust lexer + parser to walk `src/lib.rs` (or the
 * supplied `.rs` entry point) along with all `pub mod` submodules; for
 * binary `.rlib` / `.rmeta` artefacts it probes the rustc magic header so
 * that consumers can at least recognise the crate name and version.
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"

namespace polyglot::rust {

enum class CrateItemKind {
    kFunction,
    kStruct,
    kEnum,
    kTrait,
    kModule,
    kConst,
    kStatic,
    kTypeAlias,
    kMacro,
};

struct CrateItem {
    std::string     name;             // last path segment
    std::string     qualified_name;   // crate-rooted, e.g. "serde::Serializer"
    CrateItemKind   kind{CrateItemKind::kFunction};
    core::Type      type{core::Type::Any()};
    std::vector<core::Type> param_types;   // functions only
    core::Type      return_type{core::Type::Void()};
};

struct CrateInfo {
    std::string name;
    std::string version;
    std::string root_path;            // src/lib.rs or .rs entry / .rlib / .rmeta
    bool        is_binary_artifact{false};   // .rlib/.rmeta: items will be empty
    // Index keyed by qualified path (crate-rooted, e.g. "serde::ser::Serializer").
    std::unordered_map<std::string, CrateItem> items;
};

class CrateLoader {
  public:
    CrateLoader(const std::string &crate_dir,
                const std::vector<std::string> &externs,
                frontends::Diagnostics &diags);

    /// Look up a crate by name (e.g. "serde", "std").  Returns nullptr if
    /// the crate is unknown.
    const CrateInfo *ResolveCrate(const std::string &crate_name) const;

    /// Resolve a fully-qualified Rust path (e.g. "serde::Serializer").
    /// Both "::"-separated and the rust path-AST string form are accepted.
    /// Returns nullptr if the path is not exported by any loaded crate.
    const CrateItem *ResolvePath(const std::string &path) const;

    bool empty() const { return crates_.empty(); }

    /// All loaded crates, in load order.  Useful for tests/diagnostics.
    const std::vector<std::unique_ptr<CrateInfo>> &crates() const { return crates_; }

  private:
    void LoadFromPath(const std::string &explicit_name,
                      const std::string &path,
                      frontends::Diagnostics &diags);

    std::vector<std::unique_ptr<CrateInfo>> crates_;
    std::unordered_map<std::string, CrateInfo *> by_name_;
};

}  // namespace polyglot::rust
