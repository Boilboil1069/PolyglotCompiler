/**
 * @file     launch_config.cpp
 * @brief    Implementation of the launch configuration parser.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/dap/launch_config.h"

namespace polyglot::tools::ui::dap {

namespace {

LaunchRequest ParseRequest(const std::string &raw) {
  if (raw == "attach") return LaunchRequest::kAttach;
  return LaunchRequest::kLaunch;
}

LaunchConfig FromJson(const Json &cfg) {
  LaunchConfig out;
  out.name = cfg.value("name", std::string{});
  out.type = cfg.value("type", std::string{});
  out.request = ParseRequest(cfg.value("request", std::string{"launch"}));
  out.program = cfg.value("program", std::string{});
  if (cfg.contains("args") && cfg["args"].is_array()) {
    for (const auto &a : cfg["args"]) {
      if (a.is_string()) out.args.push_back(a.get<std::string>());
    }
  }
  out.cwd = cfg.value("cwd", std::string{});
  if (cfg.contains("env") && cfg["env"].is_object()) {
    for (const auto &[k, v] : cfg["env"].items()) {
      if (v.is_string()) out.env[k] = v.get<std::string>();
    }
  }
  out.extra = cfg;
  return out;
}

}  // namespace

std::vector<LaunchConfig> ParseLaunchJson(std::string_view text) {
  std::vector<LaunchConfig> out;
  Json doc;
  try {
    doc = Json::parse(text);
  } catch (const Json::parse_error &) {
    return out;
  }
  const Json *configs = nullptr;
  if (doc.is_array()) {
    configs = &doc;
  } else if (doc.is_object() && doc.contains("configurations") &&
             doc["configurations"].is_array()) {
    configs = &doc["configurations"];
  }
  if (!configs) return out;
  for (const auto &cfg : *configs) {
    if (!cfg.is_object()) continue;
    LaunchConfig parsed = FromJson(cfg);
    if (parsed.name.empty() || parsed.type.empty()) continue;
    out.push_back(std::move(parsed));
  }
  return out;
}

std::string Substitute(std::string_view text,
                       const SubstitutionContext &ctx) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    if (i + 1 < text.size() && text[i] == '$' && text[i + 1] == '{') {
      std::size_t end = text.find('}', i + 2);
      if (end == std::string::npos) { out.push_back(text[i]); ++i; continue; }
      std::string token(text.substr(i + 2, end - i - 2));
      std::string value;
      bool resolved = true;
      if (token == "workspaceFolder") {
        value = ctx.workspace_folder;
      } else if (token == "file") {
        value = ctx.file;
      } else if (token == "fileBasename") {
        value = ctx.file_basename;
      } else if (token.rfind("env:", 0) == 0) {
        auto it = ctx.env.find(token.substr(4));
        value = it != ctx.env.end() ? it->second : std::string{};
      } else if (token.rfind("command:", 0) == 0) {
        if (ctx.command_resolver) value = ctx.command_resolver(token.substr(8));
      } else {
        resolved = false;
      }
      if (resolved) {
        out.append(value);
        i = end + 1;
        continue;
      }
      // Unknown variable — keep the literal `${…}`.
      out.append(text.substr(i, end - i + 1));
      i = end + 1;
      continue;
    }
    out.push_back(text[i]);
    ++i;
  }
  return out;
}

std::vector<LaunchConfig> DefaultLaunchConfigurations() {
  std::vector<LaunchConfig> out;
  auto add = [&](const char *name, const char *type) {
    LaunchConfig c;
    c.name = name;
    c.type = type;
    c.request = LaunchRequest::kLaunch;
    c.program = "${file}";
    c.cwd = "${workspaceFolder}";
    out.push_back(std::move(c));
  };
  add("Run .ploy file", "ploy");
  add("Debug .ploy file", "ploy");
  add("Python: current file (debugpy)", "python");
  add("C/C++: current binary (lldb)", "lldb");
  add("C/C++: current binary (gdb)", "gdb");
  add("Rust: current binary (codelldb)", "codelldb");
  add("Java: current main class (jdwp)", "java");
  add(".NET: current project (netcoredbg)", "coreclr");
  return out;
}

}  // namespace polyglot::tools::ui::dap
