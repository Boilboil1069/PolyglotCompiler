#pragma once

#include <string>

namespace polyglot::core {

struct SourceLoc {
  std::string file;
  size_t line{1};
  size_t column{1};

  SourceLoc() = default;
  SourceLoc(std::string file_path, size_t line_number, size_t column_number)
      : file(std::move(file_path)), line(line_number), column(column_number) {}
};

}  // namespace polyglot::core
