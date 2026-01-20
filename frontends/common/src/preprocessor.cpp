#include "frontends/common/include/preprocessor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace polyglot::frontends {

namespace {

struct TokenCursor {
  std::vector<std::string> tokens;
  size_t index{0};

  const std::string &Peek() const {
    static const std::string kEmpty;
    if (index >= tokens.size()) {
      return kEmpty;
    }
    return tokens[index];
  }

  std::string Next() {
    if (index >= tokens.size()) {
      return {};
    }
    return tokens[index++];
  }
};

std::vector<std::string> TokenizeExpr(const std::string &expr) {
  std::vector<std::string> tokens;
  std::string current;
  for (size_t i = 0; i < expr.size(); ++i) {
    char c = expr[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '#') {
      current.push_back(c);
      continue;
    }
    if (!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
    if ((c == '&' || c == '|') && i + 1 < expr.size() && expr[i + 1] == c) {
      tokens.emplace_back(expr.substr(i, 2));
      ++i;
      continue;
    }
    tokens.emplace_back(expr.substr(i, 1));
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

int ParsePrimary(TokenCursor &cursor, const std::unordered_map<std::string, Preprocessor::Macro> &macros);
int ParseAnd(TokenCursor &cursor, const std::unordered_map<std::string, Preprocessor::Macro> &macros);
int ParseOr(TokenCursor &cursor, const std::unordered_map<std::string, Preprocessor::Macro> &macros);

int ParsePrimary(TokenCursor &cursor, const std::unordered_map<std::string, Preprocessor::Macro> &macros) {
  const std::string token = cursor.Next();
  if (token.empty()) {
    return 0;
  }
  if (token == "(") {
    int value = ParseOr(cursor, macros);
    if (cursor.Peek() == ")") {
      cursor.Next();
    }
    return value;
  }
  if (token == "defined") {
    if (cursor.Peek() == "(") {
      cursor.Next();
    }
    std::string name = cursor.Next();
    if (cursor.Peek() == ")") {
      cursor.Next();
    }
    return macros.count(name) ? 1 : 0;
  }
  if (token == "!") {
    return !ParsePrimary(cursor, macros);
  }
  if (std::isdigit(static_cast<unsigned char>(token[0]))) {
    return std::stoi(token);
  }
  auto it = macros.find(token);
  if (it != macros.end() && !it->second.body.empty() &&
      std::isdigit(static_cast<unsigned char>(it->second.body[0]))) {
    return std::stoi(it->second.body);
  }
  return macros.count(token) ? 1 : 0;
}

int ParseAnd(TokenCursor &cursor, const std::unordered_map<std::string, Preprocessor::Macro> &macros) {
  int left = ParsePrimary(cursor, macros);
  while (cursor.Peek() == "&&") {
    cursor.Next();
    int right = ParsePrimary(cursor, macros);
    left = (left && right) ? 1 : 0;
  }
  return left;
}

int ParseOr(TokenCursor &cursor, const std::unordered_map<std::string, Preprocessor::Macro> &macros) {
  int left = ParseAnd(cursor, macros);
  while (cursor.Peek() == "||") {
    cursor.Next();
    int right = ParseAnd(cursor, macros);
    left = (left || right) ? 1 : 0;
  }
  return left;
}

bool EvaluateCondition(const std::string &expr, const std::unordered_map<std::string, Preprocessor::Macro> &macros) {
  TokenCursor cursor{TokenizeExpr(expr), 0};
  return ParseOr(cursor, macros) != 0;
}

std::string TrimLeft(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
  return s;
}

std::string TrimRight(std::string s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
  return s;
}

bool IsIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool IsIdentContinue(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

Preprocessor::Preprocessor(Diagnostics &diagnostics) : diagnostics_(diagnostics) {
  file_loader_ = [](const std::string &path) -> std::optional<std::string> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      return std::nullopt;
    }
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) {
      return std::nullopt;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
  };
}

void Preprocessor::Define(const std::string &name, const std::string &value) {
  macros_[name] = Macro{false, {}, value};
}

void Preprocessor::DefineFunction(const std::string &name, std::vector<std::string> params,
                                  const std::string &value) {
  macros_[name] = Macro{true, std::move(params), value};
}

void Preprocessor::Undefine(const std::string &name) { macros_.erase(name); }

void Preprocessor::AddIncludePath(const std::string &path) { include_paths_.push_back(path); }

void Preprocessor::SetIncludePaths(std::vector<std::string> paths) { include_paths_ = std::move(paths); }

void Preprocessor::SetMaxIncludeDepth(size_t depth) { max_include_depth_ = depth; }

void Preprocessor::SetFileLoader(std::function<std::optional<std::string>(const std::string &)> loader) {
  file_loader_ = std::move(loader);
}

std::optional<std::string> Preprocessor::ReadFile(const std::string &path) const {
  if (!file_loader_) return std::nullopt;
  return file_loader_(path);
}

std::optional<std::string> Preprocessor::ResolveInclude(const std::string &target,
                                                        const std::string &current_file_dir,
                                                        bool search_include_paths) const {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!target.empty() && target[0] != '<' && target[0] != '>' && target[0] != '"') {
    fs::path p = target;
    if (p.is_relative() && !current_file_dir.empty()) {
      p = fs::path(current_file_dir) / p;
    }
    if (fs::exists(p, ec)) return p.string();
  }

  if (search_include_paths) {
    for (const auto &inc : include_paths_) {
      fs::path p = fs::path(inc) / target;
      if (fs::exists(p, ec)) return p.string();
    }
  }
  return std::nullopt;
}

std::string Preprocessor::Expand(const std::string &source) {
  std::unordered_set<std::string> guard;
  return ExpandLine(source, guard);
}

std::string Preprocessor::ExpandLine(const std::string &line, std::unordered_set<std::string> &guard) {
  std::string out;
  for (size_t i = 0; i < line.size();) {
    char c = line[i];

    // Skip strings and chars without macro expansion
    if (c == '"' || c == '\'') {
      char quote = c;
      out.push_back(c);
      ++i;
      while (i < line.size()) {
        char ch = line[i];
        out.push_back(ch);
        ++i;
        if (ch == '\\' && i < line.size()) {
          out.push_back(line[i]);
          ++i;
          continue;
        }
        if (ch == quote) break;
      }
      continue;
    }

    // Line comment
    if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
      out.append(line.substr(i));
      break;
    }

    if (IsIdentStart(c)) {
      size_t start = i;
      std::string ident;
      ident.push_back(c);
      ++i;
      while (i < line.size() && IsIdentContinue(line[i])) {
        ident.push_back(line[i]);
        ++i;
      }

      auto it = macros_.find(ident);
      if (it == macros_.end() || guard.count(ident)) {
        out.append(line.substr(start, i - start));
        continue;
      }

      const Macro &macro = it->second;
      if (!macro.is_function) {
        guard.insert(ident);
        out += ExpandLine(macro.body, guard);
        guard.erase(ident);
        continue;
      }

      // function-like: need immediate '(' after optional spaces
      size_t j = i;
      while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
      if (j >= line.size() || line[j] != '(') {
        out.append(line.substr(start, i - start));
        i = j;
        continue;
      }

      // parse arguments
      ++j;  // skip '('
      int depth = 1;
      std::vector<std::string> args;
      std::string current;
      while (j < line.size() && depth > 0) {
        char ch = line[j];
        if (ch == '(') {
          depth++;
          current.push_back(ch);
        } else if (ch == ')') {
          depth--;
          if (depth == 0) {
            args.push_back(TrimRight(TrimLeft(current)));
            current.clear();
            ++j;
            break;
          }
          current.push_back(ch);
        } else if (ch == ',' && depth == 1) {
          args.push_back(TrimRight(TrimLeft(current)));
          current.clear();
        } else {
          current.push_back(ch);
        }
        ++j;
      }
      i = j;

      if (args.size() != macro.params.size()) {
        diagnostics_.Report(core::SourceLoc{}, "Macro argument count mismatch for " + ident);
        out.append(line.substr(start, i - start));
        continue;
      }

      guard.insert(ident);
      out += SubstituteParams(macro, args, guard);
      guard.erase(ident);
      continue;
    }

    out.push_back(c);
    ++i;
  }

  return out;
}

std::string Preprocessor::SubstituteParams(const Macro &macro, const std::vector<std::string> &args,
                                           std::unordered_set<std::string> &guard) {
  std::string result;
  auto is_match = [](const std::string &s, size_t pos, const std::string &word) {
    if (pos + word.size() > s.size()) return false;
    if (s.compare(pos, word.size(), word) != 0) return false;
    auto is_ident = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
    bool left_ok = pos == 0 || !is_ident(s[pos - 1]);
    bool right_ok = (pos + word.size() >= s.size()) || !is_ident(s[pos + word.size()]);
    return left_ok && right_ok;
  };

  for (size_t i = 0; i < macro.body.size();) {
    bool replaced = false;
    for (size_t p = 0; p < macro.params.size(); ++p) {
      const auto &param = macro.params[p];
      if (is_match(macro.body, i, param)) {
        guard.insert(param);
        result += ExpandLine(args[p], guard);
        guard.erase(param);
        i += param.size();
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      result.push_back(macro.body[i]);
      ++i;
    }
  }
  return result;
}

std::string Preprocessor::Process(const std::string &source, const std::string &file) {
  return ProcessInternal(source, file, 0);
}

std::string Preprocessor::ProcessInternal(const std::string &source, const std::string &file,
                                          size_t depth) {
  struct IfState {
    bool parent_included{true};
    bool include{true};
    bool has_else{false};
  };

  if (depth > max_include_depth_) {
    diagnostics_.Report(core::SourceLoc{file, 0, 0}, "Include depth exceeded");
    return {};
  }

  std::istringstream input(source);
  std::string line;
  std::vector<IfState> stack;
  std::vector<std::string> region_stack;
  bool include_line = true;
  std::string output;
  size_t line_no = 1;

  auto current_dir = std::filesystem::path(file).parent_path().string();

  while (std::getline(input, line)) {
    std::string raw = TrimLeft(line);
    bool is_directive = !raw.empty() && raw[0] == '#';
    if (is_directive) {
      std::istringstream line_stream(raw.substr(1));
      std::string directive;
      line_stream >> directive;
      if (directive == "define") {
        if (!include_line) {
          ++line_no;
          continue;
        }
        std::string name;
        line_stream >> name;
        if (name.empty()) {
          diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Invalid #define directive");
          ++line_no;
          continue;
        }

        // function-like if immediately followed by '('
        std::vector<std::string> params;
        line_stream >> std::ws;
        if (line_stream.peek() == '(') {
          line_stream.get();
          std::string param;
          while (std::getline(line_stream, param, ',')) {
            size_t close = param.find(')');
            if (close != std::string::npos) {
              std::string last = TrimRight(TrimLeft(param.substr(0, close)));
              if (!last.empty()) params.push_back(last);
              break;
            }
            param = TrimRight(TrimLeft(param));
            if (!param.empty()) params.push_back(param);
          }
        }

        std::string value;
        std::getline(line_stream, value);
        value = TrimLeft(value);
        if (params.empty()) {
          Define(name, value);
        } else {
          DefineFunction(name, params, value);
        }
      } else if (directive == "undef") {
        if (include_line) {
          std::string name;
          line_stream >> name;
          if (!name.empty()) {
            Undefine(name);
          } else {
            diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Invalid #undef directive");
          }
        }
      } else if (directive == "include") {
        if (!include_line) {
          ++line_no;
          continue;
        }
        std::string target;
        line_stream >> target;
        if (!target.empty() && target.front() == '"' && target.back() == '"') {
          target = target.substr(1, target.size() - 2);
        } else if (!target.empty() && target.front() == '<' && target.back() == '>') {
          target = target.substr(1, target.size() - 2);
        }

        auto resolved = ResolveInclude(target, current_dir, true);
        if (!resolved) {
          diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Failed to resolve include: " + target);
          ++line_no;
          continue;
        }
        auto contents = ReadFile(*resolved);
        if (!contents) {
          diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Failed to read include: " + *resolved);
          ++line_no;
          continue;
        }

        // line/file bookkeeping
        output += "#line 1 \"" + *resolved + "\"\n";
        output += ProcessInternal(*contents, *resolved, depth + 1);
        output += "\n#line " + std::to_string(line_no + 1) + " \"" + file + "\"\n";
      } else if (directive == "ifdef" || directive == "ifndef") {
        std::string name;
        line_stream >> name;
        bool defined = macros_.find(name) != macros_.end();
        bool condition = (directive == "ifdef") ? defined : !defined;
        stack.push_back(IfState{include_line, include_line && condition, false});
        include_line = stack.back().include;
      } else if (directive == "if" || directive == "elif") {
        std::string expr;
        std::getline(line_stream, expr);
        bool condition = EvaluateCondition(expr, macros_);
        if (directive == "if") {
          stack.push_back(IfState{include_line, include_line && condition, false});
          include_line = stack.back().include;
        } else {
          if (stack.empty()) {
            diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Unmatched #elif");
          } else {
            IfState &state = stack.back();
            if (state.has_else) {
              diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Unexpected #elif after #else");
            } else {
              if (state.include) {
                state.include = false;
              } else {
                state.include = state.parent_included && condition;
              }
              include_line = state.include;
            }
          }
        }
      } else if (directive == "else") {
        if (stack.empty()) {
          diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Unmatched #else");
        } else {
          IfState &state = stack.back();
          if (state.has_else) {
            diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Duplicate #else");
          } else {
            state.has_else = true;
            state.include = state.parent_included && !state.include;
            include_line = state.include;
          }
        }
      } else if (directive == "endif") {
        if (stack.empty()) {
          diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Unmatched #endif");
        } else {
          stack.pop_back();
          include_line = stack.empty() ? true : stack.back().include;
        }
      } else if (directive == "pragma") {
        // pass-through to keep information
        if (include_line) {
          output += "#pragma ";
          std::string rest;
          std::getline(line_stream, rest);
          output += rest;
          output.push_back('\n');
        }
      } else if (directive == "region") {
        if (include_line) {
          std::string name;
          std::getline(line_stream, name);
          region_stack.push_back(TrimLeft(name));
          output += "// #region " + TrimLeft(name) + "\n";
        }
      } else if (directive == "endregion") {
        if (include_line) {
          if (region_stack.empty()) {
            diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "#endregion without #region");
          } else {
            region_stack.pop_back();
          }
          output += "// #endregion\n";
        }
      }
      ++line_no;
      continue;
    }

    if (!include_line) {
      ++line_no;
      continue;
    }

    std::unordered_set<std::string> guard;
    output += ExpandLine(line, guard);
    output.push_back('\n');
    ++line_no;
  }

  if (!stack.empty()) {
    diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Unterminated conditional directive");
  }
  if (!region_stack.empty()) {
    diagnostics_.Report(core::SourceLoc{file, line_no, 1}, "Unterminated #region");
  }

  return output;
}

}  // namespace polyglot::frontends
