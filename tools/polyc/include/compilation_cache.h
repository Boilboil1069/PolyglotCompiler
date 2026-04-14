/**
 * @file     compilation_cache.h
 * @brief    Hash-based incremental compilation cache
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-14
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace polyglot::compilation {

// Simple hash-based compilation cache.
// Stores a 64-bit hash of (source_text + compiler_settings) in a cache
// directory.  If the hash matches, the caller can skip recompilation and
// reuse the cached artefact.
class CompilationCache {
  public:
    explicit CompilationCache(const std::string &cache_dir)
        : cache_dir_(cache_dir) {
        std::error_code ec;
        std::filesystem::create_directories(cache_dir_, ec);
    }

    // Compute a 64-bit FNV-1a hash from source text and settings string.
    static uint64_t ComputeHash(const std::string &source,
                                const std::string &settings_key) {
        uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
        auto mix = [&](const char *data, size_t len) {
            for (size_t i = 0; i < len; ++i) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(data[i]));
                hash *= 1099511628211ULL;  // FNV prime
            }
        };
        mix(source.data(), source.size());
        mix(settings_key.data(), settings_key.size());
        return hash;
    }

    // Check if a cached artefact exists for the given hash.
    bool HasCached(uint64_t hash) const {
        return std::filesystem::exists(CachePath(hash));
    }

    // Read the cached artefact (returns empty string if not found).
    std::string ReadCached(uint64_t hash) const {
        auto path = CachePath(hash);
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) return {};
        auto size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        std::string content(static_cast<size_t>(size), '\0');
        ifs.read(content.data(), size);
        return content;
    }

    // Store a compiled artefact in the cache.
    void Store(uint64_t hash, const std::string &data) {
        auto path = CachePath(hash);
        std::ofstream ofs(path, std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    }

    // Purge all cached entries.
    void Clean() {
        std::error_code ec;
        std::filesystem::remove_all(cache_dir_, ec);
        std::filesystem::create_directories(cache_dir_, ec);
    }

    const std::string &CacheDir() const { return cache_dir_; }

  private:
    std::string CachePath(uint64_t hash) const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx.cache",
                      static_cast<unsigned long long>(hash));
        return (std::filesystem::path(cache_dir_) / buf).string();
    }

    std::string cache_dir_;
};

}  // namespace polyglot::compilation
