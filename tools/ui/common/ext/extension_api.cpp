/**
 * @file     extension_api.cpp
 * @brief    Implementation of the extension manifest parser,
 *           capability gate and host registry.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/ext/extension_api.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>

namespace polyglot::tools::ui::ext {

using Json = nlohmann::json;

std::string ExtensionLoaderName(ExtensionLoader l) {
  switch (l) {
    case ExtensionLoader::kNative:     return "native";
    case ExtensionLoader::kJavaScript: return "javascript";
  }
  return "native";
}

std::optional<ExtensionLoader> ExtensionLoaderFromName(
    const std::string &name) {
  if (name == "native")               return ExtensionLoader::kNative;
  if (name == "javascript" || name == "js" || name == "ts")
    return ExtensionLoader::kJavaScript;
  return std::nullopt;
}

std::string ActivationEventName(ActivationEvent e) {
  switch (e) {
    case ActivationEvent::kOnStartup:  return "onStartup";
    case ActivationEvent::kOnLanguage: return "onLanguage";
    case ActivationEvent::kOnCommand:  return "onCommand";
    case ActivationEvent::kOnView:     return "onView";
    case ActivationEvent::kOnDebug:    return "onDebug";
    case ActivationEvent::kOnFileOpen: return "onFileOpen";
  }
  return "onStartup";
}

std::optional<ActivationEvent> ActivationEventFromName(
    const std::string &name) {
  if (name == "onStartup")  return ActivationEvent::kOnStartup;
  if (name == "onLanguage") return ActivationEvent::kOnLanguage;
  if (name == "onCommand")  return ActivationEvent::kOnCommand;
  if (name == "onView")     return ActivationEvent::kOnView;
  if (name == "onDebug")    return ActivationEvent::kOnDebug;
  if (name == "onFileOpen") return ActivationEvent::kOnFileOpen;
  return std::nullopt;
}

std::string CapabilityName(Capability c) {
  switch (c) {
    case Capability::kFilesystem: return "filesystem";
    case Capability::kNetwork:    return "network";
    case Capability::kProcess:    return "process";
    case Capability::kClipboard:  return "clipboard";
    case Capability::kSecrets:    return "secrets";
  }
  return "filesystem";
}

std::optional<Capability> CapabilityFromName(const std::string &name) {
  if (name == "filesystem") return Capability::kFilesystem;
  if (name == "network")    return Capability::kNetwork;
  if (name == "process")    return Capability::kProcess;
  if (name == "clipboard")  return Capability::kClipboard;
  if (name == "secrets")    return Capability::kSecrets;
  return std::nullopt;
}

std::string ContributionKindName(ContributionKind k) {
  switch (k) {
    case ContributionKind::kCommand:          return "command";
    case ContributionKind::kKeybinding:       return "keybinding";
    case ContributionKind::kMenu:             return "menu";
    case ContributionKind::kPanel:            return "panel";
    case ContributionKind::kView:             return "view";
    case ContributionKind::kStatusBarItem:    return "statusBarItem";
    case ContributionKind::kTheme:            return "theme";
    case ContributionKind::kLanguageClient:   return "languageClient";
    case ContributionKind::kDebugAdapter:     return "debugAdapter";
    case ContributionKind::kFileIconTheme:    return "fileIconTheme";
    case ContributionKind::kFormatter:        return "formatter";
    case ContributionKind::kSnippet:          return "snippet";
    case ContributionKind::kTask:             return "task";
    case ContributionKind::kRefactorProvider: return "refactorProvider";
  }
  return "command";
}

namespace {

std::optional<ContributionKind> ContributionKindFromName(
    const std::string &name) {
  static const std::unordered_map<std::string, ContributionKind> kMap = {
      {"commands",          ContributionKind::kCommand},
      {"keybindings",       ContributionKind::kKeybinding},
      {"menus",             ContributionKind::kMenu},
      {"panels",            ContributionKind::kPanel},
      {"views",             ContributionKind::kView},
      {"statusBarItems",    ContributionKind::kStatusBarItem},
      {"themes",            ContributionKind::kTheme},
      {"languageClients",   ContributionKind::kLanguageClient},
      {"debugAdapters",     ContributionKind::kDebugAdapter},
      {"fileIconThemes",    ContributionKind::kFileIconTheme},
      {"formatters",        ContributionKind::kFormatter},
      {"snippets",          ContributionKind::kSnippet},
      {"tasks",             ContributionKind::kTask},
      {"refactorProviders", ContributionKind::kRefactorProvider},
  };
  auto it = kMap.find(name);
  if (it == kMap.end()) return std::nullopt;
  return it->second;
}

std::vector<int> ParseSemver(const std::string &v) {
  std::vector<int> parts;
  int cur = 0;
  bool any = false;
  for (char c : v) {
    if (c >= '0' && c <= '9') {
      cur = cur * 10 + (c - '0');
      any = true;
    } else if (c == '.') {
      parts.push_back(any ? cur : 0);
      cur = 0;
      any = false;
    } else {
      break;
    }
  }
  if (any) parts.push_back(cur);
  return parts;
}

}  // namespace

int CompareVersion(const std::string &a, const std::string &b) {
  auto pa = ParseSemver(a);
  auto pb = ParseSemver(b);
  size_t n = std::max(pa.size(), pb.size());
  pa.resize(n, 0);
  pb.resize(n, 0);
  for (size_t i = 0; i < n; ++i) {
    if (pa[i] < pb[i]) return -1;
    if (pa[i] > pb[i]) return 1;
  }
  return 0;
}

std::string ExtensionStateName(ExtensionState s) {
  switch (s) {
    case ExtensionState::kInstalled: return "installed";
    case ExtensionState::kActivated: return "activated";
    case ExtensionState::kDisabled:  return "disabled";
    case ExtensionState::kFailed:    return "failed";
  }
  return "installed";
}

std::optional<ExtensionManifest> ParseManifest(const std::string &json) {
  Json doc;
  try {
    doc = Json::parse(json);
  } catch (const Json::parse_error &) {
    return std::nullopt;
  }
  if (!doc.is_object()) return std::nullopt;
  ExtensionManifest m;
  m.id          = doc.value("id", std::string{});
  m.name        = doc.value("name", std::string{});
  m.version     = doc.value("version", std::string{});
  m.publisher   = doc.value("publisher", std::string{});
  m.description = doc.value("description", std::string{});
  m.entry_point = doc.value("entry_point", std::string{});
  if (m.entry_point.empty())
    m.entry_point = doc.value("main", std::string{});
  if (m.id.empty() || m.version.empty() || m.entry_point.empty())
    return std::nullopt;

  auto loader = doc.value("loader", std::string{"native"});
  if (auto k = ExtensionLoaderFromName(loader)) m.loader = *k;

  if (doc.contains("activation") && doc["activation"].is_array()) {
    for (const auto &t : doc["activation"]) {
      Trigger trig;
      if (t.is_string()) {
        if (auto e = ActivationEventFromName(t.get<std::string>()))
          trig.event = *e;
        m.activation.push_back(trig);
      } else if (t.is_object()) {
        std::string evt = t.value("event", std::string{"onStartup"});
        if (auto e = ActivationEventFromName(evt)) trig.event = *e;
        trig.argument = t.value("argument", std::string{});
        m.activation.push_back(trig);
      }
    }
  }

  if (doc.contains("capabilities") && doc["capabilities"].is_array()) {
    for (const auto &c : doc["capabilities"]) {
      if (c.is_string())
        if (auto cap = CapabilityFromName(c.get<std::string>()))
          m.required_capabilities.push_back(*cap);
    }
  }

  if (doc.contains("contributes") && doc["contributes"].is_object()) {
    for (auto it = doc["contributes"].begin();
         it != doc["contributes"].end(); ++it) {
      auto kind = ContributionKindFromName(it.key());
      if (!kind || !it.value().is_array()) continue;
      for (const auto &item : it.value()) {
        Contribution con;
        con.kind = *kind;
        if (item.is_object()) {
          con.id = item.value("id", std::string{});
          con.title = item.value("title", std::string{});
          for (auto pit = item.begin(); pit != item.end(); ++pit) {
            if (pit.key() == "id" || pit.key() == "title") continue;
            if (pit.value().is_string())
              con.properties[pit.key()] = pit.value().get<std::string>();
          }
        } else if (item.is_string()) {
          con.id = item.get<std::string>();
        }
        if (!con.id.empty()) m.contributes.push_back(std::move(con));
      }
    }
  }

  return m;
}

void CapabilityGate::Grant(const std::string &id, Capability c) {
  grants_[id].insert(static_cast<int>(c));
}
void CapabilityGate::Revoke(const std::string &id, Capability c) {
  auto it = grants_.find(id);
  if (it == grants_.end()) return;
  it->second.erase(static_cast<int>(c));
}
bool CapabilityGate::IsGranted(const std::string &id, Capability c) const {
  auto it = grants_.find(id);
  return it != grants_.end() && it->second.count(static_cast<int>(c));
}
bool CapabilityGate::AllGranted(
    const std::string &id, const std::vector<Capability> &needed) const {
  for (auto c : needed)
    if (!IsGranted(id, c)) return false;
  return true;
}

bool ExtensionHost::Install(ExtensionManifest manifest) {
  auto it = records_.find(manifest.id);
  if (it != records_.end()) {
    if (CompareVersion(manifest.version, it->second.manifest.version) <= 0)
      return false;
    DropContributionsOf(manifest.id);
    it->second.manifest = std::move(manifest);
    it->second.state = ExtensionState::kInstalled;
    it->second.error.clear();
    return true;
  }
  ExtensionRecord rec;
  rec.manifest = std::move(manifest);
  records_[rec.manifest.id] = std::move(rec);
  return true;
}

bool ExtensionHost::Uninstall(const std::string &id) {
  auto it = records_.find(id);
  if (it == records_.end()) return false;
  DropContributionsOf(id);
  records_.erase(it);
  return true;
}

bool ExtensionHost::Activate(const std::string &id) {
  auto it = records_.find(id);
  if (it == records_.end()) return false;
  auto &rec = it->second;
  if (rec.state == ExtensionState::kDisabled) return false;
  if (gate_ && !gate_->AllGranted(id, rec.manifest.required_capabilities)) {
    rec.state = ExtensionState::kFailed;
    rec.error = "missing capability grant";
    return false;
  }
  DropContributionsOf(id);
  RegisterContributions(rec);
  rec.state = ExtensionState::kActivated;
  rec.error.clear();
  return true;
}

bool ExtensionHost::Deactivate(const std::string &id) {
  auto it = records_.find(id);
  if (it == records_.end()) return false;
  DropContributionsOf(id);
  if (it->second.state == ExtensionState::kActivated)
    it->second.state = ExtensionState::kInstalled;
  return true;
}

bool ExtensionHost::Reload(const std::string &id) {
  if (!Deactivate(id)) return false;
  return Activate(id);
}

std::optional<ExtensionRecord> ExtensionHost::Get(
    const std::string &id) const {
  auto it = records_.find(id);
  if (it == records_.end()) return std::nullopt;
  return it->second;
}

std::vector<ExtensionRecord> ExtensionHost::List() const {
  std::vector<ExtensionRecord> out;
  out.reserve(records_.size());
  for (const auto &kv : records_) out.push_back(kv.second);
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return a.manifest.id < b.manifest.id;
  });
  return out;
}

std::vector<Contribution> ExtensionHost::Contributions() const {
  std::vector<Contribution> out;
  out.reserve(registry_.size());
  for (const auto &kv : registry_) out.push_back(kv.second.second);
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  return out;
}

std::vector<Contribution> ExtensionHost::ContributionsOfKind(
    ContributionKind k) const {
  std::vector<Contribution> out;
  for (const auto &kv : registry_)
    if (kv.second.second.kind == k) out.push_back(kv.second.second);
  std::sort(out.begin(), out.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  return out;
}

bool ExtensionHost::MatchesActivationEvent(
    const std::string &id, ActivationEvent event,
    const std::string &argument) const {
  auto it = records_.find(id);
  if (it == records_.end()) return false;
  for (const auto &t : it->second.manifest.activation) {
    if (t.event != event) continue;
    if (t.argument.empty() || t.argument == argument) return true;
  }
  return false;
}

void ExtensionHost::RegisterContributions(const ExtensionRecord &rec) {
  for (const auto &c : rec.manifest.contributes) {
    Key k{static_cast<int>(c.kind), c.id};
    // Last-installed wins; the registry dedupes per (kind, id) so
    // a re-activation refreshes the entry instead of duplicating.
    registry_[k] = std::make_pair(rec.manifest.id, c);
  }
}

void ExtensionHost::DropContributionsOf(const std::string &id) {
  for (auto it = registry_.begin(); it != registry_.end();) {
    if (it->second.first == id) it = registry_.erase(it);
    else                        ++it;
  }
}

}  // namespace polyglot::tools::ui::ext
