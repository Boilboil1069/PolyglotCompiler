/**
 * @file     pyi_loader.h
 * @brief    Loader for Python `.pyi` (typeshed) stub files.
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Resolves dotted module names against a list of search paths populated from
 * `--python-stubs=<dir>` driver options, returning a `PyiModule` whose
 * `exports` map is consumed by the Python semantic analyser to populate a
 * module's exported symbol set.  Submodules and `from ... import ...`
 * dependencies are followed transitively.
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"

namespace polyglot::python {

/// One exported name in a `.pyi` stub.  Functions also expose their parameter
/// list so that future call-site checking can be added without re-parsing.
struct PyiSymbol {
    std::string name;
    core::Type  type{core::Type::Any()};
    bool        is_function{false};
    bool        is_class{false};
    bool        is_module{false};
    std::vector<core::Type> param_types;
    core::Type  return_type{core::Type::Any()};
};

struct PyiModule {
    std::string name;        // dotted, e.g. "os.path"
    std::string source_path; // absolute path to the .pyi that defined it
    std::unordered_map<std::string, PyiSymbol> exports;
};

class PyiLoader {
  public:
    PyiLoader(std::vector<std::string> stub_paths,
              frontends::Diagnostics &diags);

    /// Resolve a dotted module name (e.g. "math", "os.path", "collections").
    /// Returns nullptr when no stub file can be found for the module.  The
    /// returned pointer is owned by the loader and remains valid for the
    /// loader's lifetime.
    const PyiModule *Resolve(const std::string &module_name);

    bool empty() const { return search_paths_.empty(); }

    const std::vector<std::string> &search_paths() const { return search_paths_; }

  private:
    std::optional<std::string> LocateStubFile(const std::string &module_name) const;

    std::unique_ptr<PyiModule> ParseStubFile(const std::string &path,
                                             const std::string &module_name);

    std::vector<std::string> search_paths_;
    frontends::Diagnostics  &diags_;
    std::unordered_map<std::string, std::unique_ptr<PyiModule>> cache_;
    // Sentinel used to avoid re-parsing modules whose stub was missing.
    std::unordered_map<std::string, bool> missing_;
};

}  // namespace polyglot::python
