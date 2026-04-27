/**
 * @file     effective_settings_loader.cpp
 * @brief    Implementation of the shared CLI/UI settings loader
 *
 * @ingroup  Tool / common
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "tools/common/include/effective_settings_loader.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

#if defined(_WIN32)
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace polyglot::tools::common {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path HomeDir() {
#if defined(_WIN32)
  if (const char *p = std::getenv("USERPROFILE"); p && *p) return fs::path(p);
  if (const char *h = std::getenv("HOMEPATH"); h && *h) {
    if (const char *d = std::getenv("HOMEDRIVE")) return fs::path(std::string(d) + h);
  }
#else
  if (const char *p = std::getenv("HOME"); p && *p) return fs::path(p);
#endif
  return fs::current_path();
}

#if defined(_WIN32)
fs::path AppDataDir() {
  if (const char *p = std::getenv("APPDATA"); p && *p) return fs::path(p);
  return HomeDir() / "AppData" / "Roaming";
}
#endif

std::string ReadFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool ParseJson(const std::string &text, json *out, SettingsDiagnostic *err) {
  try {
    *out = json::parse(text, nullptr, /*allow_exceptions=*/true,
                       /*ignore_comments=*/true);
    return true;
  } catch (const json::parse_error &e) {
    if (err) {
      err->message = e.what();
      err->is_error = true;
    }
    return false;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

fs::path UserSettingsPath() {
#if defined(_WIN32)
  return AppDataDir() / "PolyglotCompiler" / "settings.json";
#elif defined(__APPLE__)
  return HomeDir() / "Library" / "Application Support" / "PolyglotCompiler" / "settings.json";
#else
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return fs::path(xdg) / "PolyglotCompiler" / "settings.json";
  }
  return HomeDir() / ".config" / "PolyglotCompiler" / "settings.json";
#endif
}

fs::path UserKeybindingsPath() {
  return UserSettingsPath().parent_path() / "keybindings.json";
}

fs::path WorkspaceSettingsPath(const fs::path &workspace_root) {
  if (workspace_root.empty()) return {};
  return workspace_root / ".polyglot" / "settings.json";
}

// ---------------------------------------------------------------------------
// Merge helpers
// ---------------------------------------------------------------------------

void DeepMerge(json &base, const json &override_) {
  if (!override_.is_object() || !base.is_object()) {
    base = override_;
    return;
  }
  for (auto it = override_.begin(); it != override_.end(); ++it) {
    auto found = base.find(it.key());
    if (found != base.end() && found->is_object() && it->is_object()) {
      DeepMerge(*found, *it);
    } else {
      base[it.key()] = *it;
    }
  }
}

json GetByDottedKey(const json &tree, const std::string &dotted_key) {
  // Settings keys are stored as flat dotted strings (e.g. "editor.tabSize")
  // in the canonical layout, so try a flat lookup first.
  if (tree.is_object()) {
    auto it = tree.find(dotted_key);
    if (it != tree.end()) return *it;
  }
  // Fallback: nested object path.
  const json *cur = &tree;
  size_t start = 0;
  while (start < dotted_key.size()) {
    size_t dot = dotted_key.find('.', start);
    std::string segment =
        dotted_key.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!cur->is_object()) return json();
    auto it = cur->find(segment);
    if (it == cur->end()) return json();
    cur = &(*it);
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return *cur;
}

void SetByDottedKey(json &tree, const std::string &dotted_key, const json &value) {
  // Canonical storage: flat dotted keys at the top level.
  if (!tree.is_object()) tree = json::object();
  tree[dotted_key] = value;
}

// ---------------------------------------------------------------------------
// Schema validation (light-weight; does not require json-schema-validator lib)
// ---------------------------------------------------------------------------

namespace {

bool ValidateValue(const std::string &key, const json &value, const json &spec,
                   std::vector<SettingsDiagnostic> *diagnostics) {
  auto add = [&](const std::string &msg) {
    if (diagnostics) {
      SettingsDiagnostic d;
      d.scope = "schema";
      d.message = "settings['" + key + "']: " + msg;
      d.is_error = true;
      diagnostics->push_back(std::move(d));
    }
  };

  if (auto t = spec.find("type"); t != spec.end() && t->is_string()) {
    const std::string ty = t->get<std::string>();
    bool ok = (ty == "string"  && value.is_string()) ||
              (ty == "integer" && value.is_number_integer()) ||
              (ty == "number"  && value.is_number()) ||
              (ty == "boolean" && value.is_boolean()) ||
              (ty == "array"   && value.is_array()) ||
              (ty == "object"  && value.is_object());
    if (!ok) {
      add("expected type '" + ty + "', got '" + std::string(value.type_name()) + "'");
      return false;
    }
  }
  if (auto e = spec.find("enum"); e != spec.end() && e->is_array()) {
    bool found = false;
    for (const auto &allowed : *e) {
      if (allowed == value) { found = true; break; }
    }
    if (!found) {
      add("value not in enum");
      return false;
    }
  }
  if (value.is_number_integer()) {
    if (auto m = spec.find("minimum"); m != spec.end()) {
      if (value.get<long long>() < m->get<long long>()) {
        add("value below minimum");
        return false;
      }
    }
    if (auto m = spec.find("maximum"); m != spec.end()) {
      if (value.get<long long>() > m->get<long long>()) {
        add("value above maximum");
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool ValidateAgainstSchema(const json &data, const std::string &schema_json,
                           std::vector<SettingsDiagnostic> *diagnostics) {
  json schema;
  SettingsDiagnostic perr;
  perr.scope = "schema";
  if (!ParseJson(schema_json, &schema, &perr)) {
    if (diagnostics) {
      perr.message = "schema parse failed: " + perr.message;
      diagnostics->push_back(perr);
    }
    return false;
  }
  bool ok = true;
  auto props = schema.find("properties");
  if (props == schema.end() || !props->is_object()) return true;  // nothing to check
  for (auto it = data.begin(); it != data.end(); ++it) {
    auto spec = props->find(it.key());
    if (spec == props->end()) continue;  // additionalProperties allowed
    if (!ValidateValue(it.key(), *it, *spec, diagnostics)) ok = false;
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Top-level loaders
// ---------------------------------------------------------------------------

EffectiveSettings LoadEffectiveSettingsExplicit(const std::string &defaults_json,
                                                const std::string &schema_json,
                                                const fs::path &user_path,
                                                const fs::path &workspace_path) {
  EffectiveSettings out;

  // Layer 1: defaults (must parse).
  {
    SettingsDiagnostic err;
    err.scope = "default";
    if (!ParseJson(defaults_json, &out.defaults, &err)) {
      err.message = "default settings parse failed: " + err.message;
      out.diagnostics.push_back(err);
      out.defaults = json::object();
    }
  }

  // Layer 2: user.
  out.user = json::object();
  if (!user_path.empty() && fs::exists(user_path)) {
    SettingsDiagnostic err;
    err.scope = "user";
    err.file = user_path.string();
    const std::string text = ReadFile(user_path);
    if (!ParseJson(text, &out.user, &err)) {
      err.message = "user settings parse failed: " + err.message;
      out.diagnostics.push_back(err);
      out.user = json::object();
    }
  }

  // Layer 3: workspace.
  out.workspace = json::object();
  if (!workspace_path.empty() && fs::exists(workspace_path)) {
    SettingsDiagnostic err;
    err.scope = "workspace";
    err.file = workspace_path.string();
    const std::string text = ReadFile(workspace_path);
    if (!ParseJson(text, &out.workspace, &err)) {
      err.message = "workspace settings parse failed: " + err.message;
      out.diagnostics.push_back(err);
      out.workspace = json::object();
    }
  }

  // Merge.
  out.effective = out.defaults;
  DeepMerge(out.effective, out.user);
  DeepMerge(out.effective, out.workspace);

  // Schema-validate every layer that contributed values.
  ValidateAgainstSchema(out.user, schema_json, &out.diagnostics);
  ValidateAgainstSchema(out.workspace, schema_json, &out.diagnostics);

  return out;
}

EffectiveSettings LoadEffectiveSettings(const std::string &defaults_json,
                                        const std::string &schema_json,
                                        const fs::path &workspace_root) {
  return LoadEffectiveSettingsExplicit(defaults_json, schema_json,
                                       UserSettingsPath(),
                                       WorkspaceSettingsPath(workspace_root));
}

std::string PrettyPrint(const json &tree) { return tree.dump(4); }

// ---------------------------------------------------------------------------
// CLI helper
// ---------------------------------------------------------------------------

std::optional<int> HandleSettingsCliFlags(int argc, char **argv) {
  std::filesystem::path explicit_user;
  bool print = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--print-effective-settings") {
      print = true;
    } else if (a == "--settings" && i + 1 < argc) {
      explicit_user = argv[++i];
    } else if (a.rfind("--settings=", 0) == 0) {
      explicit_user = a.substr(std::string("--settings=").size());
    }
  }
  if (!print) return std::nullopt;

  // Walk up from cwd looking for a workspace marker (.polyglot/).
  std::filesystem::path ws;
  for (auto p = std::filesystem::current_path(); !p.empty() && p != p.root_path();
       p = p.parent_path()) {
    if (std::filesystem::exists(p / ".polyglot")) { ws = p; break; }
  }

  EffectiveSettings eff =
      explicit_user.empty()
          ? LoadEffectiveSettings("{}", "{}", ws)
          : LoadEffectiveSettingsExplicit("{}", "{}", explicit_user,
                                          WorkspaceSettingsPath(ws));
  std::cout << PrettyPrint(eff.effective) << std::endl;
  for (const auto &d : eff.diagnostics) {
    std::cerr << "[settings] " << d.scope << ": " << d.message << std::endl;
  }
  return 0;
}

}  // namespace polyglot::tools::common
