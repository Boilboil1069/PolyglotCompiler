#include "frontends/common/include/preprocessor.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::frontends {

std::string Preprocessor::Expand(const std::string &source) {
  std::string output;
  output.reserve(source.size());
  std::string token;
  for (char c : source) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      token.push_back(c);
      continue;
    }
    if (!token.empty()) {
      auto it = macros_.find(token);
      if (it != macros_.end()) {
        output += it->second;
      } else {
        output += token;
      }
      token.clear();
    }
    output.push_back(c);
  }
  if (!token.empty()) {
    auto it = macros_.find(token);
    output += (it != macros_.end()) ? it->second : token;
  }
  return output;
}

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

int ParsePrimary(TokenCursor &cursor,
                 const std::unordered_map<std::string, std::string> &macros);
int ParseAnd(TokenCursor &cursor,
             const std::unordered_map<std::string, std::string> &macros);
int ParseOr(TokenCursor &cursor,
            const std::unordered_map<std::string, std::string> &macros);

int ParsePrimary(TokenCursor &cursor,
                 const std::unordered_map<std::string, std::string> &macros) {
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
  if (it != macros.end() && !it->second.empty() &&
      std::isdigit(static_cast<unsigned char>(it->second[0]))) {
    return std::stoi(it->second);
  }
  return macros.count(token) ? 1 : 0;
}

int ParseAnd(TokenCursor &cursor,
             const std::unordered_map<std::string, std::string> &macros) {
  int left = ParsePrimary(cursor, macros);
  while (cursor.Peek() == "&&") {
    cursor.Next();
    int right = ParsePrimary(cursor, macros);
    left = (left && right) ? 1 : 0;
  }
  return left;
}

int ParseOr(TokenCursor &cursor,
            const std::unordered_map<std::string, std::string> &macros) {
  int left = ParseAnd(cursor, macros);
  while (cursor.Peek() == "||") {
    cursor.Next();
    int right = ParseAnd(cursor, macros);
    left = (left || right) ? 1 : 0;
  }
  return left;
}

bool EvaluateCondition(const std::string &expr,
                       const std::unordered_map<std::string, std::string> &macros) {
  TokenCursor cursor{TokenizeExpr(expr), 0};
  return ParseOr(cursor, macros) != 0;
}

}  // namespace

std::string Preprocessor::Process(const std::string &source) {
  struct IfState {
    bool parent_included{true};
    bool include{true};
    bool has_else{false};
  };

  std::istringstream input(source);
  std::string line;
  std::vector<IfState> stack;
  bool include_line = true;
  std::string output;

  auto trim_left = [](std::string &value) {
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), [](unsigned char c) {
                  return !std::isspace(c);
                }));
  };

  while (std::getline(input, line)) {
    std::string raw = line;
    trim_left(raw);
    bool is_directive = !raw.empty() && raw[0] == '#';
    if (is_directive) {
      std::istringstream line_stream(raw.substr(1));
      std::string directive;
      line_stream >> directive;
      if (directive == "define") {
        if (!include_line) {
          continue;
        }
        std::string name;
        line_stream >> name;
        std::string value;
        std::getline(line_stream, value);
        trim_left(value);
        if (!name.empty()) {
          Define(name, value);
        } else {
          diagnostics_.Report(core::SourceLoc{}, "Invalid #define directive");
        }
      } else if (directive == "undef") {
        if (!include_line) {
          continue;
        }
        std::string name;
        line_stream >> name;
        if (!name.empty()) {
          Undefine(name);
        } else {
          diagnostics_.Report(core::SourceLoc{}, "Invalid #undef directive");
        }
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
            diagnostics_.Report(core::SourceLoc{}, "Unmatched #elif");
            continue;
          }
          IfState &state = stack.back();
          if (state.has_else) {
            diagnostics_.Report(core::SourceLoc{}, "Unexpected #elif after #else");
            continue;
          }
          if (state.include) {
            state.include = false;
          } else {
            state.include = state.parent_included && condition;
          }
          include_line = state.include;
        }
      } else if (directive == "else") {
        if (stack.empty()) {
          diagnostics_.Report(core::SourceLoc{}, "Unmatched #else");
          continue;
        }
        IfState &state = stack.back();
        if (state.has_else) {
          diagnostics_.Report(core::SourceLoc{}, "Duplicate #else");
          continue;
        }
        state.has_else = true;
        state.include = state.parent_included && !state.include;
        include_line = state.include;
      } else if (directive == "endif") {
        if (stack.empty()) {
          diagnostics_.Report(core::SourceLoc{}, "Unmatched #endif");
          continue;
        }
        stack.pop_back();
        include_line = stack.empty() ? true : stack.back().include;
      } else {
        if (!include_line) {
          continue;
        }
      }
      continue;
    }

    if (!include_line) {
      continue;
    }

    output += Expand(line);
    output.push_back('\n');
  }

  if (!stack.empty()) {
    diagnostics_.Report(core::SourceLoc{}, "Unterminated conditional directive");
  }

  return output;
}

}  // namespace polyglot::frontends
