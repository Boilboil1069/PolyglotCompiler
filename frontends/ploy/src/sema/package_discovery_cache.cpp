#include "frontends/ploy/include/package_discovery_cache.h"

namespace polyglot::ploy {

// ============================================================================
// Cache Key Construction
// ============================================================================

std::string PackageDiscoveryCache::MakeKey(const std::string &language,
                                           const std::string &manager,
                                           const std::string &env_path) {
    // Canonical key: "language|manager|env_path"
    return language + "|" + manager + "|" + env_path;
}

// ============================================================================
// Query
// ============================================================================

bool PackageDiscoveryCache::HasDiscovered(const std::string &key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return discovered_keys_.count(key) > 0;
}

std::unordered_map<std::string, PackageInfo>
PackageDiscoveryCache::Retrieve(const std::string &key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    return {};
}

// ============================================================================
// Mutation
// ============================================================================

void PackageDiscoveryCache::Store(
    const std::string &key,
    const std::unordered_map<std::string, PackageInfo> &packages) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[key] = packages;
    discovered_keys_.insert(key);
}

void PackageDiscoveryCache::MarkDiscovered(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    discovered_keys_.insert(key);
}

void PackageDiscoveryCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    discovered_keys_.clear();
}

} // namespace polyglot::ploy
