/**
 * @file     ruby_import_resolver.h
 * @brief    Ruby `require` / `require_relative` / `load` / `autoload` resolver.
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-27
 *
 * The resolver discovers real third-party Ruby files reachable through:
 *   - `require_relative "path"` — resolved relative to the importer file
 *     directory, with `.rb` extension probing.
 *   - `require "name"` / `load "name"` — resolved through (in priority):
 *       1. `RUBYLIB` environment variable
 *       2. User supplied `--gem-path` roots (each treated as a Bundler
 *          / RubyGems vendor directory containing `<gem>/lib/<file>.rb`)
 *       3. Each gem-path root used directly as a `$LOAD_PATH` entry
 *       4. The host Ruby's default `$LOAD_PATH` (probed once via
 *          `ruby -e "puts $LOAD_PATH"` — optional, never fatal)
 *   - `autoload :Const, "path"` — recorded as a deferred require.
 *
 * Loaded files are parsed with `RbLexer` + `RbParser`; top-level
 * `MethodDecl`, `ClassDecl` and `ModuleDecl` declarations form the
 * exported symbol set.  Class / module bodies are recursed once so that
 * `Foo::bar` calls resolve to the right qualified name.
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"

namespace polyglot::ruby {

/** @brief Kind of an exported Ruby symbol. */
enum class RbSymbolKind {
    kMethod,
    kClass,
    kModule,
    kConstant,
};

/** @brief A symbol exported by a resolved Ruby file. */
struct RbSymbol {
    std::string name;                    ///< Bare name (e.g. "process").
    std::string qualified_name;          ///< Module-qualified name (e.g. "Foo::Bar.process").
    RbSymbolKind kind{RbSymbolKind::kMethod};
    std::vector<core::Type> param_types;
    std::vector<std::string> param_names;
    core::Type return_type;
    bool is_singleton{false};            ///< `def self.foo` or class-level constant.
};

/** @brief A resolved Ruby file (one per absolute path). */
struct RbFile {
    std::string requested;               ///< Original specifier (e.g. "json").
    std::string resolved_path;           ///< Absolute file path on disk.
    std::string gem_name;                ///< Empty for stdlib / project files.
    std::unordered_map<std::string, RbSymbol> exports;
};

/**
 * @brief Resolves Ruby `require`-family statements to source files and
 *        harvests their top-level exports.
 */
class RbImportResolver {
  public:
    RbImportResolver(std::string project_dir,
                     std::vector<std::string> gem_paths,
                     frontends::Diagnostics &diagnostics);

    /**
     * @brief Resolve a `require` / `require_relative` / `load` specifier.
     *
     * @param specifier      The literal string passed to require.
     * @param importer_dir   Directory of the file performing the require.
     * @param relative       True if `require_relative`.
     * @return Borrowed pointer; nullptr on failure (a diagnostic is emitted).
     */
    const RbFile *Resolve(const std::string &specifier,
                          const std::string &importer_dir,
                          bool relative);

  private:
    std::string project_dir_;
    std::vector<std::string> gem_paths_;
    std::vector<std::string> rubylib_paths_;
    std::vector<std::string> default_load_paths_;
    bool default_paths_probed_{false};
    frontends::Diagnostics &diagnostics_;

    std::unordered_map<std::string, RbFile *> by_key_;
    std::unordered_map<std::string, RbFile *> by_path_;
    std::vector<std::unique_ptr<RbFile>> files_;

    void EnsureDefaultLoadPaths();
    std::string ProbeRb(const std::string &path_no_ext) const;
    std::string ResolveRelative(const std::string &specifier,
                                const std::string &importer_dir) const;
    std::string ResolveBare(const std::string &specifier,
                            std::string &out_gem_name) const;
    std::unique_ptr<RbFile> LoadFile(const std::string &requested,
                                      const std::string &resolved_path,
                                      const std::string &gem_name);
};

}  // namespace polyglot::ruby
