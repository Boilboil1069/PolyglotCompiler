/**
 * @file     frontend_registry.cpp
 * @brief    FrontendRegistry singleton implementation
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/common/include/frontend_registry.h"

#include <algorithm>
#include <filesystem>
#include <cctype>

namespace polyglot::frontends {

// ============================================================================
// Singleton access
// ============================================================================

FrontendRegistry &FrontendRegistry::Instance() {
    static FrontendRegistry instance;
    return instance;
}

// ============================================================================
// Helper: lowercase a string for case-insensitive matching
// ============================================================================

static std::string ToLower(const std::string &s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// ============================================================================
// Register
// ============================================================================

void FrontendRegistry::Register(std::shared_ptr<ILanguageFrontend> frontend) {
    if (!frontend) return;
    std::lock_guard<std::mutex> lock(mutex_);

    std::string name = frontend->Name();
    frontends_[name] = frontend;

    // Register aliases
    for (const auto &alias : frontend->Aliases()) {
        alias_map_[ToLower(alias)] = name;
    }

    // Register file extensions
    for (const auto &ext : frontend->Extensions()) {
        extension_map_[ToLower(ext)] = name;
    }
}

// ============================================================================
// Lookup by name / alias
// ============================================================================

const ILanguageFrontend *FrontendRegistry::GetFrontend(const std::string &language) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = ToLower(language);

    // Direct canonical name match
    auto it = frontends_.find(key);
    if (it != frontends_.end()) return it->second.get();

    // Alias lookup
    auto alias_it = alias_map_.find(key);
    if (alias_it != alias_map_.end()) {
        auto canonical_it = frontends_.find(alias_it->second);
        if (canonical_it != frontends_.end()) return canonical_it->second.get();
    }

    return nullptr;
}

// ============================================================================
// Lookup by extension
// ============================================================================

const ILanguageFrontend *FrontendRegistry::GetFrontendByExtension(const std::string &extension) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = ToLower(extension);
    auto it = extension_map_.find(key);
    if (it != extension_map_.end()) {
        auto frontend_it = frontends_.find(it->second);
        if (frontend_it != frontends_.end()) return frontend_it->second.get();
    }
    return nullptr;
}

// ============================================================================
// Detect language from file path
// ============================================================================

std::string FrontendRegistry::DetectLanguage(const std::string &file_path) const {
    auto ext = std::filesystem::path(file_path).extension().string();
    if (ext.empty()) return {};

    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = ToLower(ext);
    auto it = extension_map_.find(key);
    if (it != extension_map_.end()) return it->second;
    return {};
}

// ============================================================================
// Enumerate
// ============================================================================

std::vector<std::string> FrontendRegistry::SupportedLanguages() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(frontends_.size());
    for (const auto &[name, _] : frontends_) {
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<const ILanguageFrontend *> FrontendRegistry::AllFrontends() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<const ILanguageFrontend *> result;
    result.reserve(frontends_.size());
    for (const auto &[_, frontend] : frontends_) {
        result.push_back(frontend.get());
    }
    return result;
}

// ============================================================================
// Clear
// ============================================================================

void FrontendRegistry::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    frontends_.clear();
    alias_map_.clear();
    extension_map_.clear();
}

}  // namespace polyglot::frontends
