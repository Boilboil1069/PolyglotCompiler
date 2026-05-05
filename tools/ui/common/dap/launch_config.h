/**
 * @file     launch_config.h
 * @brief    `.polyc/launch.json` parser (demand 2026-04-28-28 §3).
 *
 * Parses VS-Code-flavoured launch configurations into a structured
 * vector that the IDE shell can render in the run/debug picker and
 * pass to `DapClient::Request("launch"|"attach", …)`.
 *
 * Variable substitution covers `${workspaceFolder}`, `${file}`,
 * `${fileBasename}`, `${env:NAME}` and `${command:NAME}` (the last
 * delegates to a caller-supplied resolver).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::dap {

using Json = nlohmann::json;

enum class LaunchRequest {
  kLaunch,
  kAttach,
};

struct LaunchConfig {
  std::string name;
  std::string type;          ///< `ploy`, `python`, `cppdbg`, `lldb`, …
  LaunchRequest request{LaunchRequest::kLaunch};
  std::string program;
  std::vector<std::string> args;
  std::string cwd;
  std::map<std::string, std::string> env;
  Json extra;                ///< unparsed adapter-specific options
};

/// Parses the contents of a `launch.json` file (or VSCode-style
/// `{"version":..., "configurations":[...]}` envelope).  Returns the
/// list of decoded configurations; invalid entries are skipped.
std::vector<LaunchConfig> ParseLaunchJson(std::string_view text);

/// Variable resolver used by `Substitute` for `${command:…}`
/// expressions.  Returns the empty string when the command is
/// unknown, which matches VS Code's behaviour.
using VariableResolver = std::function<std::string(const std::string &name)>;

struct SubstitutionContext {
  std::string workspace_folder;
  std::string file;
  std::string file_basename;
  std::map<std::string, std::string> env;
  VariableResolver command_resolver;
};

/// Replaces `${…}` placeholders inside `text`.  Unknown variables
/// are left as-is so the user sees what was intended.
std::string Substitute(std::string_view text,
                       const SubstitutionContext &ctx);

/// Built-in starter configurations promised by demand 2026-04-28-28
/// §3 ("默认提供 …").  Returned in stable order so the picker UI
/// looks deterministic across runs.
std::vector<LaunchConfig> DefaultLaunchConfigurations();

}  // namespace polyglot::tools::ui::dap
