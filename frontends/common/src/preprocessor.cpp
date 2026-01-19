#include "frontends/common/include/preprocessor.h"

#include <cctype>

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

}  // namespace polyglot::frontends
