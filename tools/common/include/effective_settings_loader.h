/**
 * @file     effective_settings_loader.h
 * @brief    Shared 3-layer settings loader (default / user / workspace)
 *
 * Used by both the polyui IDE and the CLI tools (polyc, polyld, polybench,
 * polyrt, polytopo) so a single source of truth governs how settings.json
 * files are discovered, parsed, validated and merged.
 *
 * Layering (lowest priority first):
 *   1. Default settings (resource embedded; loaded from string passed by caller).
 *   2. User settings (~/.config/PolyglotCompiler/settings.json or platform equivalent).
 *   3. Workspace settings (<workspace>/.polyglot/settings.json).
 *
 * @ingroup  Tool / common
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::common {

struct SettingsDiagnostic {
  std::string scope;    ///< "default" | "user" | "workspace" | "schema"
  std::string file;     ///< Source file (may be empty)
  int line{-1};
  int column{-1};
  std::string message;
  bool is_error{true};
};

struct EffectiveSettings {
  nlohmann::json defaults;            ///< Layer 1
  nlohmann::json user;                ///< Layer 2 (only fields explicitly set)
  nlohmann::json workspace;           ///< Layer 3 (only fields explicitly set)
  nlohmann::json effective;           ///< Merged tree (defaults < user < workspace)
  std::vector<SettingsDiagnostic> diagnostics;
};

/**
 * @brief Resolve the user-level settings.json path for the current OS.
 *
 *   Windows : %APPDATA%\PolyglotCompiler\settings.json
 *   macOS   : ~/Library/Application Support/PolyglotCompiler/settings.json
 *   Linux   : ~/.config/PolyglotCompiler/settings.json
 */
std::filesystem::path UserSettingsPath();

/**
 * @brief Resolve workspace settings file under <workspace>/.polyglot/settings.json.
 *        Returns an empty path if @p workspace_root is empty.
 */
std::filesystem::path WorkspaceSettingsPath(const std::filesystem::path &workspace_root);

/**
 * @brief Resolve the user-level keybindings.json path (sibling of settings.json).
 */
std::filesystem::path UserKeybindingsPath();

/**
 * @brief Load and merge all three layers.
 *
 * @param defaults_json   Raw text of the embedded default settings JSON.
 * @param schema_json     Raw text of the schema JSON used for validation.
 * @param workspace_root  Optional workspace root for layer 3.  May be empty.
 */
EffectiveSettings LoadEffectiveSettings(const std::string &defaults_json,
                                        const std::string &schema_json,
                                        const std::filesystem::path &workspace_root = {});

/**
 * @brief Same as LoadEffectiveSettings() but lets the caller supply explicit
 *        user/workspace settings file paths (used by `--settings <path>`).
 */
EffectiveSettings LoadEffectiveSettingsExplicit(const std::string &defaults_json,
                                                const std::string &schema_json,
                                                const std::filesystem::path &user_path,
                                                const std::filesystem::path &workspace_path);

/**
 * @brief Validate the @p data tree against @p schema_json.  Errors are
 *        appended to @p diagnostics.  Returns true on success.
 */
bool ValidateAgainstSchema(const nlohmann::json &data,
                           const std::string &schema_json,
                           std::vector<SettingsDiagnostic> *diagnostics);

/**
 * @brief Merge @p override into @p base (deep merge for objects, replace for arrays/scalars).
 */
void DeepMerge(nlohmann::json &base, const nlohmann::json &override_);

/**
 * @brief Read a dotted-path key (e.g. "editor.tabSize") from @p tree.
 *        Returns the JSON null value if absent.
 */
nlohmann::json GetByDottedKey(const nlohmann::json &tree, const std::string &dotted_key);

/**
 * @brief Set a dotted-path key, creating intermediate objects as needed.
 */
void SetByDottedKey(nlohmann::json &tree, const std::string &dotted_key,
                    const nlohmann::json &value);

/**
 * @brief Pretty-print the effective settings tree (4-space indent, sorted keys).
 */
std::string PrettyPrint(const nlohmann::json &tree);

/**
 * @brief CLI helper: handles `--settings <path>` and `--print-effective-settings`.
 *
 * Usage at the top of any CLI tool's main():
 * @code
 *   if (auto rc = HandleSettingsCliFlags(argc, argv); rc.has_value()) return *rc;
 * @endcode
 *
 * Returns std::nullopt when no relevant flag was present (the caller should
 * proceed normally), or an exit code when the helper has already printed the
 * effective tree (or an error) to stdout/stderr.
 */
std::optional<int> HandleSettingsCliFlags(int argc, char **argv);

}  // namespace polyglot::tools::common
