/**
 * @file     javascript_import_resolver.h
 * @brief    JavaScript / TypeScript module resolver (Node.js algorithm).
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-27
 *
 * Implements the Node.js module-resolution algorithm against the
 * compiler's own JavaScript lexer/parser, exposing the exported symbols
 * of every resolved module as a queryable index.  TypeScript declaration
 * files (`.d.ts` / `index.d.ts`) are preferred over `.js` sources because
 * they carry curated, JSDoc-quality signatures that downstream type
 * inference and IR lowering can consume verbatim.
 *
 * Resolution order, mirroring `require.resolve()` semantics:
 *   1. Relative specifiers (`./foo`, `../bar`) are resolved against the
 *      directory of the importing file (extension probing in
 *      `.d.ts → .ts → .mjs → .cjs → .js`).
 *   2. Bare specifiers (`lodash`, `@scope/pkg`) walk every
 *      `<file_dir>/node_modules` ancestor directory until the project
 *      root or filesystem root is reached.
 *   3. Each `--node-modules=<dir>` supplied via FrontendOptions is
 *      consulted as an extra root after #2 fails.
 *   4. The package's `package.json` is consulted for `types`/`typings`,
 *      `module`, then `main`; if none is present the resolver falls back
 *      to `index.{d.ts,js,mjs,cjs}`.
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"

namespace polyglot::javascript {

enum class JsSymbolKind {
    kFunction,
    kClass,
    kVariable,
    kConstant,
    kTypeAlias,
    kNamespace,
};

/**
 * @brief A single exported symbol from a resolved JS/TS module.
 *
 * `imported_as` is the foreign name (the name visible inside the module);
 * `local_alias` may be set by the caller after binding to record the
 * importer-side alias for diagnostic purposes.
 */
struct JsSymbol {
    std::string name;            // export name as seen by importers
    std::string qualified_name;  // "<package>.<name>" or "<file>.<name>"
    JsSymbolKind kind{JsSymbolKind::kFunction};
    bool is_default{false};

    // Function/method signature (only valid for kFunction).
    std::vector<core::Type> param_types;
    std::vector<std::string> param_names;
    core::Type return_type{core::Type::Any()};
};

/**
 * @brief A resolved JavaScript module (single file or package entry).
 */
struct JsModule {
    std::string specifier;       // raw import specifier
    std::string resolved_path;   // absolute file path of the loaded entry
    std::string package_name;    // "" for relative imports, package name otherwise
    std::string package_version; // value of "version" in package.json (if any)
    bool is_typescript_decl{false};   // true when resolved_path ends in .d.ts
    bool is_commonjs{false};          // true for .cjs / detected `module.exports`

    // Index keyed by export name (default export uses the empty key "" or
    // explicit "default" — both are populated for ergonomic lookup).
    std::unordered_map<std::string, JsSymbol> exports;
};

/**
 * @brief JavaScript / TypeScript module resolver.
 *
 * Holds an internal cache keyed by `<importer_dir>::<specifier>` so that
 * the same import is resolved at most once per compilation unit.
 */
class JsImportResolver {
  public:
    JsImportResolver(std::string project_dir,
                     std::vector<std::string> extra_node_modules,
                     frontends::Diagnostics &diagnostics);

    /// Resolve a specifier against the directory of the importing file.
    /// Returns nullptr (and reports a diagnostic) when resolution fails.
    /// Returned pointer remains valid for the lifetime of the resolver.
    const JsModule *Resolve(const std::string &specifier,
                            const std::string &importer_dir);

    /// Look up a previously-resolved module without performing I/O.
    const JsModule *Lookup(const std::string &specifier,
                           const std::string &importer_dir) const;

    /// All node_modules roots in the order the resolver consults them
    /// (configured project + ancestor walk + extras).
    const std::vector<std::string> &ExtraNodeModulePaths() const { return extra_node_modules_; }

    const std::vector<std::unique_ptr<JsModule>> &modules() const { return modules_; }

  private:
    std::string ResolveRelative(const std::string &specifier,
                                const std::string &importer_dir) const;
    std::string ResolveBare(const std::string &specifier,
                            const std::string &importer_dir,
                            std::string &out_pkg_name,
                            std::string &out_pkg_version) const;
    std::string ProbeExtensions(const std::string &path_no_ext) const;
    std::string ResolvePackageEntry(const std::string &pkg_dir) const;

    std::unique_ptr<JsModule> LoadModule(const std::string &specifier,
                                          const std::string &resolved_path,
                                          const std::string &pkg_name,
                                          const std::string &pkg_version);

    static std::string CacheKey(const std::string &specifier,
                                const std::string &importer_dir);

    std::string project_dir_;
    std::vector<std::string> extra_node_modules_;
    frontends::Diagnostics &diagnostics_;

    std::vector<std::unique_ptr<JsModule>> modules_;
    std::unordered_map<std::string, JsModule *> by_key_;
    // De-dup loaded files by absolute path.
    std::unordered_map<std::string, JsModule *> by_path_;
};

}  // namespace polyglot::javascript
