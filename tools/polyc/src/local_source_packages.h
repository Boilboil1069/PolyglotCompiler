/**
 * @file local_source_packages.h
 * @brief Deterministic project-vendored source package bundling for polyc.
 */
#pragma once

#include <string>

namespace polyglot::tools {

/**
 * Build a temporary single-frontend source unit from `consumer_source` and
 * every matching project-local `IMPORT <language> PACKAGE <name>` manifest
 * that declares a `source` file.
 *
 * The project root is the directory containing `poly_entry`.  Manifests are
 * read from `packages/<package>/poly.package.toml`.  If no matching source
 * package exists, the original consumer path is returned unless
 * `require_local` is true.  Strict local mode requires every declared package
 * for `language` to resolve to exactly one matching manifest with a source
 * file and a compatible manifest version.  On a malformed, ambiguous, unsafe,
 * or incompatible matching manifest, an empty string is returned and `error`
 * is set.
 */
std::string BuildVendoredSourceBundle(const std::string &poly_entry,
                                      const std::string &language,
                                      const std::string &consumer_source,
                                      const std::string &aux_dir,
                                      std::string *error = nullptr,
                                      bool require_local = false);

} // namespace polyglot::tools
