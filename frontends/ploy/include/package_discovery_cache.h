#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontends/ploy/include/ploy_sema.h"

namespace polyglot::ploy {

// ============================================================================
// PackageDiscoveryCache
//
// Session-level, thread-safe cache for package-discovery results.  A single
// instance can be shared across multiple PloySema compilations so that
// external commands (pip list, cargo install --list, …) are only executed once
// per unique (language, manager, env_path) triple.
//
// The cache key is built from the concatenation:
//     language + "|" + manager_kind_string + "|" + env_path
// ============================================================================

class PackageDiscoveryCache {
  public:
    // Build the canonical cache key from the three discovery parameters.
    static std::string MakeKey(const std::string &language,
                               const std::string &manager,
                               const std::string &env_path);

    // Check whether discovery for the given key has already been completed.
    bool HasDiscovered(const std::string &key) const;

    // Store all packages discovered under the given key.  Thread-safe.
    void Store(const std::string &key,
               const std::unordered_map<std::string, PackageInfo> &packages);

    // Retrieve cached packages for a previously-discovered key.
    // Returns an empty map if the key is not present.
    std::unordered_map<std::string, PackageInfo> Retrieve(const std::string &key) const;

    // Mark a key as discovered (even if no packages were found) to prevent
    // repeated external-command invocations.
    void MarkDiscovered(const std::string &key);

    // Clear all cached entries (useful for testing).
    void Clear();

  private:
    mutable std::mutex mutex_;
    // Key → discovered packages
    std::unordered_map<std::string,
                       std::unordered_map<std::string, PackageInfo>> cache_;
    // Keys for which discovery has been attempted (including empty results)
    std::unordered_set<std::string> discovered_keys_;
};

} // namespace polyglot::ploy
