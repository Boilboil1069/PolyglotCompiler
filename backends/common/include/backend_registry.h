/**
 * @file     backend_registry.h
 * @brief    Global registry of code-generation backends
 *
 * Singleton lookup table that maps a target triple (or any registered alias)
 * to the corresponding @c ITargetBackend implementation.  Concrete backends
 * self-register at static-initialisation time using the
 * @c REGISTER_TARGET_BACKEND() helper, mirroring the
 * @c polyglot::frontends::FrontendRegistry pattern.
 *
 * Thread-safety: every public method takes the internal mutex; backend
 * instances themselves are expected to be reentrant per
 * @c ITargetBackend's contract.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "backends/common/include/target_backend.h"

namespace polyglot::backends {

// ============================================================================
// BackendRegistry — process-global registration center
// ============================================================================

/** @brief Outcome of a registration attempt, surfaced to callers and tests. */
enum class RegisterStatus {
  kOk,
  kDuplicateTriple, // A backend with the same canonical triple is already registered.
  kAliasConflict,   // One of the aliases collides with another backend.
  kNullBackend,     // A null pointer was passed.
};

/** @brief BackendRegistry singleton. */
class BackendRegistry {
public:
  /** @brief Access the process-wide singleton. */
  static BackendRegistry &Instance();

  /**
   * @brief Register a concrete backend.
   *
   * The registry takes shared ownership; callers may also retain a reference
   * to interact with the backend directly.  Duplicate triples / alias
   * collisions are rejected (returning @c RegisterStatus::kDuplicateTriple
   * or @c kAliasConflict) and the registry is left untouched.
   */
  RegisterStatus Register(std::shared_ptr<ITargetBackend> backend);

  /**
   * @brief Look up a backend by canonical triple or alias.
   *
   * Matching is case-insensitive on ASCII.  Returns @c nullptr when no
   * backend matches @p triple_or_alias.
   */
  ITargetBackend *Find(const std::string &triple_or_alias) const;

  /**
   * @brief Convenience wrapper around @c Find that, on failure, returns
   *        @c nullptr and writes a human-readable diagnostic listing all
   *        available triples to @p out_diagnostic.
   */
  ITargetBackend *FindOrDiagnose(const std::string &triple_or_alias,
                                 std::string *out_diagnostic) const;

  /** @brief List metadata for every registered backend (sorted by triple). */
  std::vector<BackendInfo> List() const;

  /** @brief Number of currently registered backends. */
  std::size_t Size() const;

  /** @brief Drop every registration (intended for tests). */
  void Clear();

private:
  BackendRegistry() = default;

  // Non-copyable, non-movable singleton.
  BackendRegistry(const BackendRegistry &) = delete;
  BackendRegistry &operator=(const BackendRegistry &) = delete;

  mutable std::mutex mutex_;

  // Canonical triple -> backend.  std::map would also work but unordered_map
  // matches the FrontendRegistry style and is sufficient: List() sorts on
  // demand for stable presentation.
  std::unordered_map<std::string, std::shared_ptr<ITargetBackend>> backends_;

  // Lower-case alias -> canonical triple.
  std::unordered_map<std::string, std::string> alias_map_;
};

// ============================================================================
// Static auto-registration helper
// ============================================================================

/** @brief RAII helper that registers a backend at static initialisation. */
struct BackendRegistrar {
  explicit BackendRegistrar(std::shared_ptr<ITargetBackend> backend);
};

// Convenience macro for self-registering backends from a .cpp translation
// unit.  Usage:
//     REGISTER_TARGET_BACKEND(std::make_shared<MyTargetBackend>());
#define REGISTER_TARGET_BACKEND(backend_ptr)                                                       \
  static ::polyglot::backends::BackendRegistrar s_target_backend_registrar_##__LINE__(backend_ptr)

// ============================================================================
// JSON helpers (for --print-targets / target.json)
// ============================================================================

/** @brief Render a @c BackendInfo as compact JSON. */
std::string ToJson(const BackendInfo &info);

/** @brief Render a list of @c BackendInfo as a JSON array. */
std::string ToJson(const std::vector<BackendInfo> &infos);

/** @brief Render a @c BackendInfo as a multi-line human-readable block. */
std::string ToHumanReadable(const BackendInfo &info);

} // namespace polyglot::backends
