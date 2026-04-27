/**
 * @file     go_import_resolver.h
 * @brief    Go package import resolver (go.mod + GOROOT/std + GOPATH/pkg/mod).
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-27
 *
 * Resolves the import paths embedded in `import "..."` declarations against
 * the configured Go project layout, emitting an indexed view of every
 * exported package-level symbol so that semantic analysis and IR lowering
 * can verify cross-package calls and bind them to the appropriate runtime
 * bridge symbol (`__ploy_go_call`).
 *
 * Search order (mirrors `cmd/go`):
 *   1. `<project_dir>/<import-path>`     — internal sub-packages
 *   2. `<project_dir>/vendor/<import-path>` — vendored dependencies
 *   3. `<GOROOT>/src/<import-path>`      — standard library
 *   4. `<GOPATH>/pkg/mod/<escaped>@<ver>/` — module cache (per `go.sum`)
 *   5. Each `--go-mod-cache=<dir>` supplied via FrontendOptions
 *
 * The resolver intentionally relies on the project's own GoLexer and
 * GoParser when scanning `.go` source files, so the returned signatures
 * stay 100% consistent with what the rest of the compiler observes.
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"

namespace polyglot::go {

enum class GoSymbolKind {
    kFunction,
    kStruct,
    kInterface,
    kVariable,
    kConstant,
    kTypeAlias,
};

/**
 * @brief A single exported symbol contributed by an external Go package.
 *
 * `qualified_name` uses the canonical "<pkg-name>.<symbol>" form (Go's own
 * convention), e.g. `fmt.Println`.  Method receivers are folded into
 * `qualified_name` as `<pkg>.<Type>.<Method>`.
 */
struct GoSymbol {
    std::string name;            // unqualified symbol name
    std::string qualified_name;  // "<pkg>.<symbol>" or "<pkg>.<Type>.<Method>"
    GoSymbolKind kind{GoSymbolKind::kFunction};

    // Function signature (only valid for kFunction).
    std::vector<core::Type> param_types;
    std::vector<std::string> param_names;
    core::Type return_type{core::Type::Void()};
    bool is_variadic{false};

    // Receiver type for methods (empty for free functions / non-methods).
    std::string receiver_type;
};

/**
 * @brief Resolved external Go package, indexed by exported symbol name.
 *
 * The same `GoPackage` object is reused for every `import` referencing the
 * same canonical path; consumers must therefore treat it as immutable.
 */
struct GoPackage {
    std::string import_path;     // e.g. "github.com/user/proj/foo"
    std::string package_name;    // last segment / `package` clause
    std::string root_dir;        // absolute directory the package was loaded from
    std::string module_version;  // e.g. "v1.2.3" (empty for std/local)
    bool is_stdlib{false};
    bool is_local{false};
    bool source_available{false};

    // Index keyed by simple symbol name (e.g. "Println").
    std::unordered_map<std::string, GoSymbol> exports;
};

/**
 * @brief Parsed `go.mod` data for the active module.
 */
struct GoModuleManifest {
    std::string module_path;     // first `module` directive
    std::string go_version;      // `go 1.21` directive (raw text)
    // Each `require` entry: import-path -> version
    std::unordered_map<std::string, std::string> requires_;
    // Each `replace` entry: original-path -> replacement directory or path
    std::unordered_map<std::string, std::string> replaces;
    bool loaded{false};
};

/**
 * @brief Go package import resolver.
 *
 * Stateless across compilation runs (callers must hold their own instance).
 * The first call to `Resolve()` for a given import path performs source
 * discovery and parsing; subsequent calls hit an internal cache.
 */
class GoImportResolver {
  public:
    GoImportResolver(std::string project_dir,
                     std::vector<std::string> extra_module_paths,
                     frontends::Diagnostics &diagnostics);

    /// Resolve an import path to a populated `GoPackage`.  Returns nullptr
    /// when the path cannot be located (a diagnostic has already been
    /// reported).  The returned pointer is owned by the resolver and stays
    /// valid for its lifetime.
    const GoPackage *Resolve(const std::string &import_path);

    /// Look up an already-resolved package (no I/O).  Useful for tests.
    const GoPackage *Lookup(const std::string &import_path) const;

    /// Read-only access to the parsed module manifest, if any.
    const GoModuleManifest &Manifest() const { return manifest_; }

    /// Effective list of module-cache search directories.  Order follows
    /// the search precedence documented at the top of this header.
    const std::vector<std::string> &ModuleSearchPaths() const { return module_paths_; }

    /// All packages discovered by this resolver instance, in load order.
    const std::vector<std::unique_ptr<GoPackage>> &packages() const { return packages_; }

  private:
    void LoadGoMod();
    std::string LocatePackageDir(const std::string &import_path) const;
    std::unique_ptr<GoPackage> LoadPackage(const std::string &import_path,
                                           const std::string &dir,
                                           const std::string &version,
                                           bool is_local,
                                           bool is_stdlib);
    static std::string EscapeModulePath(const std::string &p);

    std::string project_dir_;
    std::vector<std::string> module_paths_;
    frontends::Diagnostics &diagnostics_;
    GoModuleManifest manifest_{};

    std::vector<std::unique_ptr<GoPackage>> packages_;
    std::unordered_map<std::string, GoPackage *> by_path_;
};

}  // namespace polyglot::go
