#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace polyglot::tools {

std::string Assemble(const std::string &source) {
  namespace fs = std::filesystem;

  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path asm_path = fs::temp_directory_path() /
                            ("polyasm_" + std::to_string(stamp) + ".s");
  const fs::path obj_path = fs::temp_directory_path() /
                            ("polyasm_" + std::to_string(stamp) + ".o");

  std::ofstream asm_file(asm_path);
  if (!asm_file) {
    throw std::runtime_error("failed to open temp asm file");
  }
  asm_file << source;
  asm_file.close();

  const std::string command = "clang -c \"" + asm_path.string() +
                              "\" -o \"" + obj_path.string() + "\"";
  const int rc = std::system(command.c_str());
  fs::remove(asm_path);
  if (rc != 0) {
    throw std::runtime_error("clang failed to assemble input");
  }

  return obj_path.string();
}

}  // namespace polyglot::tools

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: polyasm <input.s> [output.o]\n";
    return 1;
  }

  const std::filesystem::path input_path(argv[1]);
  std::ifstream input(input_path);
  if (!input) {
    std::cerr << "Failed to open " << input_path << "\n";
    return 1;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();

  std::string object_path;
  try {
    object_path = polyglot::tools::Assemble(buffer.str());
  } catch (const std::exception &ex) {
    std::cerr << "Assemble failed: " << ex.what() << "\n";
    return 1;
  }

  if (argc >= 3) {
    const std::filesystem::path requested(argv[2]);
    std::error_code ec;
    const auto parent = requested.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
      ec.clear();
    }

    std::filesystem::rename(object_path, requested, ec);
    if (ec) {
      ec.clear();
      std::filesystem::copy_file(object_path, requested,
                                 std::filesystem::copy_options::overwrite_existing,
                                 ec);
      if (!ec) {
        std::filesystem::remove(object_path);
      }
    }
    if (!ec) {
      object_path = requested.string();
    }
  }

  std::cout << object_path << "\n";
  return 0;
}
