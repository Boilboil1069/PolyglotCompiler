/**
 * @file     metadata_reader.h
 * @brief    ECMA-335 CLI metadata reader for .NET PE assemblies.
 *
 * @ingroup  Frontend / DotNet
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"

namespace polyglot::dotnet {

// ============================================================================
// Decoded metadata (subset useful for symbol-table population)
// ============================================================================
struct CliFieldMeta {
    std::string name;
    std::uint16_t flags{0};
    core::Type type{core::Type::Any()};
};

struct CliMethodMeta {
    std::string name;
    std::uint16_t flags{0};
    std::uint16_t impl_flags{0};
    core::Type return_type{core::Type::Void()};
    std::vector<core::Type> param_types;
};

struct CliTypeMeta {
    std::uint32_t flags{0};
    std::string name;            // simple name, e.g. "Console"
    std::string ns;              // dotted, e.g. "System"
    std::string full_name;       // ns + "." + name (or just name)
    std::vector<CliFieldMeta>  fields;
    std::vector<CliMethodMeta> methods;
};

struct CliAssemblyRef {
    std::string name;
    std::uint16_t major{0};
    std::uint16_t minor{0};
    std::uint16_t build{0};
    std::uint16_t revision{0};
    std::string culture;
};

struct CliAssembly {
    std::string path;
    std::string name;            // assembly name (from Assembly table)
    std::vector<CliTypeMeta> types;
    std::vector<CliAssemblyRef> references;
    // Index: namespace → indices into `types`.
    std::unordered_map<std::string, std::vector<std::size_t>> ns_index;
};

// Parse a managed assembly (.dll / .exe).  Returns nullopt and emits a
// diagnostic if the file is not a CLR PE image or is malformed.
std::optional<CliAssembly>
ReadAssemblyMetadata(const std::string &path,
                     frontends::Diagnostics &diags);

// ============================================================================
// AssemblyLoader — load multiple references, resolve namespace lookups.
// ============================================================================
class AssemblyLoader {
  public:
    AssemblyLoader(const std::vector<std::string> &references,
                   frontends::Diagnostics &diags);

    // List all CliTypeMeta declared in `namespace_name` across every loaded
    // reference, in the order they were discovered.
    std::vector<const CliTypeMeta *> ListNamespace(const std::string &namespace_name) const;

    // Resolve a fully-qualified type name (e.g. "System.Console"); returns
    // nullptr if not found.
    const CliTypeMeta *ResolveType(const std::string &full_name) const;

    bool empty() const { return assemblies_.empty(); }

  private:
    std::vector<CliAssembly> assemblies_;
};

}  // namespace polyglot::dotnet
