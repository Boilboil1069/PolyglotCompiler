/**
 * @file     frontend_registry.h
 * @brief    Unified frontend registration center
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/common/include/language_frontend.h"

namespace polyglot::frontends {

// ============================================================================
// FrontendRegistry — singleton registration center
// ============================================================================

/** @brief FrontendRegistry class. */
class FrontendRegistry {
  public:
    // Access the global singleton instance.
    static FrontendRegistry &Instance();

    // Register a frontend.  Ownership is shared; the registry keeps a
    // shared_ptr so that callers may also hold a reference if needed.
    // Thread-safe.
    void Register(std::shared_ptr<ILanguageFrontend> frontend);

    // Look up a frontend by canonical name or alias (e.g. "cpp", "csharp").
    // Returns nullptr if not found.
    const ILanguageFrontend *GetFrontend(const std::string &language) const;

    // Look up a frontend by file extension (e.g. ".py", ".cpp").
    // The extension must include the leading dot and is case-insensitive.
    // Returns nullptr if not found.
    const ILanguageFrontend *GetFrontendByExtension(const std::string &extension) const;

    // Detect the language for a given file path based on its extension.
    // Returns the canonical language name, or empty string if unknown.
    std::string DetectLanguage(const std::string &file_path) const;

    // Return a list of all registered canonical language names.
    std::vector<std::string> SupportedLanguages() const;

    // Return a list of all registered frontends.
    std::vector<const ILanguageFrontend *> AllFrontends() const;

    // Clear all registrations (mainly for testing).
    void Clear();

  private:
    FrontendRegistry() = default;

    // Non-copyable, non-movable
    FrontendRegistry(const FrontendRegistry &) = delete;
    FrontendRegistry &operator=(const FrontendRegistry &) = delete;

    mutable std::mutex mutex_;

    // Canonical name -> frontend
    std::unordered_map<std::string, std::shared_ptr<ILanguageFrontend>> frontends_;

    // Alias -> canonical name (e.g. "csharp" -> "dotnet")
    std::unordered_map<std::string, std::string> alias_map_;

    // Extension -> canonical name (e.g. ".py" -> "python")
    std::unordered_map<std::string, std::string> extension_map_;
};

// ============================================================================
// FrontendRegistrar — RAII helper for static auto-registration
// ============================================================================

/** @brief FrontendRegistrar data structure. */
struct FrontendRegistrar {
    explicit FrontendRegistrar(std::shared_ptr<ILanguageFrontend> frontend) {
        FrontendRegistry::Instance().Register(std::move(frontend));
    }
};

// Convenience macro for auto-registration in a .cpp translation unit.
// Usage:  REGISTER_FRONTEND(std::make_shared<CppLanguageFrontend>());
#define REGISTER_FRONTEND(frontend_ptr) \
    static ::polyglot::frontends::FrontendRegistrar \
        s_frontend_registrar_##__LINE__(frontend_ptr)

}  // namespace polyglot::frontends
