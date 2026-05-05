/**
 * @file     extension_api.h
 * @brief    Extension manifest, contribution registry and host
 *           lifecycle for the PolyUI extension system.
 *
 * Extensions ship as self-describing units (`extension.json` +
 * payload) and register contributions against a strongly-typed
 * registry.  Two payload kinds are supported out of the box —
 * native dynamic libraries (C / C++) and JavaScript / TypeScript
 * bundles loaded into an embedded engine — but the host treats
 * both uniformly through `ExtensionHost::Activate`.
 *
 * Security: every extension declares the capabilities it needs
 * (`filesystem`, `network`, `process`, ...) in its manifest.  The
 * host refuses to grant a capability the user has not approved.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace polyglot::tools::ui::ext {

enum class ExtensionLoader {
  kNative,        ///< Dynamic library (C/C++).
  kJavaScript,    ///< Bundled JS/TS for the embedded engine.
};

std::string ExtensionLoaderName(ExtensionLoader l);
std::optional<ExtensionLoader> ExtensionLoaderFromName(
    const std::string &name);

enum class ActivationEvent {
  kOnStartup,
  kOnLanguage,
  kOnCommand,
  kOnView,
  kOnDebug,
  kOnFileOpen,
};

std::string ActivationEventName(ActivationEvent e);
std::optional<ActivationEvent> ActivationEventFromName(
    const std::string &name);

enum class Capability {
  kFilesystem,
  kNetwork,
  kProcess,
  kClipboard,
  kSecrets,
};

std::string CapabilityName(Capability c);
std::optional<Capability> CapabilityFromName(const std::string &name);

/// Every shippable contribution kind.  The contribution registry
/// dedupes per `(kind, id)`.
enum class ContributionKind {
  kCommand,
  kKeybinding,
  kMenu,
  kPanel,
  kView,
  kStatusBarItem,
  kTheme,
  kLanguageClient,
  kDebugAdapter,
  kFileIconTheme,
  kFormatter,
  kSnippet,
  kTask,
  kRefactorProvider,
};

std::string ContributionKindName(ContributionKind k);

struct Contribution {
  ContributionKind kind{ContributionKind::kCommand};
  std::string id;            ///< Unique id within `kind`.
  std::string title;
  std::unordered_map<std::string, std::string> properties;
};

struct Trigger {
  ActivationEvent event{ActivationEvent::kOnStartup};
  std::string argument;      ///< Language id, command id, view id, ...
};

/// Parsed `extension.json`.
struct ExtensionManifest {
  std::string id;            ///< Unique extension id ("publisher.name").
  std::string name;
  std::string version;       ///< Semver string.
  std::string publisher;
  std::string description;
  std::string entry_point;   ///< Library path or JS bundle path.
  ExtensionLoader loader{ExtensionLoader::kNative};
  std::vector<Trigger> activation;
  std::vector<Capability> required_capabilities;
  std::vector<Contribution> contributes;
};

/// Parse an `extension.json` blob.  Returns `nullopt` when the
/// manifest is missing required fields (`id`, `version`,
/// `entry_point`).
std::optional<ExtensionManifest> ParseManifest(const std::string &json);

/// Compare two semver strings ("1.2.3" vs "1.10.0").  Returns
/// negative / zero / positive like `strcmp`.
int CompareVersion(const std::string &a, const std::string &b);

enum class ExtensionState {
  kInstalled,    ///< Present on disk but not loaded.
  kActivated,    ///< Loaded and contributions registered.
  kDisabled,     ///< Suppressed by user.
  kFailed,       ///< Activation failed.
};

std::string ExtensionStateName(ExtensionState s);

struct ExtensionRecord {
  ExtensionManifest manifest;
  ExtensionState state{ExtensionState::kInstalled};
  std::string error;
};

/// Capability-grant store consulted by the host before activation.
class CapabilityGate {
 public:
  void Grant(const std::string &extension_id, Capability c);
  void Revoke(const std::string &extension_id, Capability c);
  bool IsGranted(const std::string &extension_id, Capability c) const;
  /// True iff every capability in `needed` is granted.
  bool AllGranted(const std::string &extension_id,
                  const std::vector<Capability> &needed) const;

 private:
  std::unordered_map<std::string, std::unordered_set<int>> grants_;
};

/// Host: owns activated extensions, the contribution registry and
/// the capability gate.  Activation is gated on capability grants;
/// reactivation refreshes the registry without leaving stale
/// contributions.
class ExtensionHost {
 public:
  void set_capability_gate(CapabilityGate *gate) { gate_ = gate; }

  /// Install (record only) the extension.  Returns false when the
  /// id is already installed at the same or newer version.
  bool Install(ExtensionManifest manifest);
  bool Uninstall(const std::string &extension_id);

  /// Activate: requires installed + every capability granted.
  /// `Reload` deactivates and re-activates without removing the
  /// install record.
  bool Activate(const std::string &extension_id);
  bool Deactivate(const std::string &extension_id);
  bool Reload(const std::string &extension_id);

  std::optional<ExtensionRecord> Get(const std::string &id) const;
  std::vector<ExtensionRecord> List() const;

  /// All registered contributions, deduplicated per `(kind, id)`.
  std::vector<Contribution> Contributions() const;
  std::vector<Contribution> ContributionsOfKind(ContributionKind k) const;

  /// True iff `extension_id` reacts to `event`.
  bool MatchesActivationEvent(const std::string &extension_id,
                              ActivationEvent event,
                              const std::string &argument = {}) const;

 private:
  CapabilityGate *gate_{nullptr};
  std::unordered_map<std::string, ExtensionRecord> records_;
  // Active contributions keyed by (kindIndex, id) with extension owner.
  struct Key {
    int kind;
    std::string id;
    bool operator==(const Key &o) const {
      return kind == o.kind && id == o.id;
    }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const noexcept {
      return std::hash<std::string>()(k.id) ^ static_cast<size_t>(k.kind);
    }
  };
  std::unordered_map<Key, std::pair<std::string, Contribution>, KeyHash>
      registry_;

  void RegisterContributions(const ExtensionRecord &rec);
  void DropContributionsOf(const std::string &extension_id);
};

}  // namespace polyglot::tools::ui::ext
