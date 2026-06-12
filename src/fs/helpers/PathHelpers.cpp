#include "fs/helpers/PathHelpers.hpp"

#include <filesystem>
#include <stdexcept>

std::string normalizeFsPath(const std::filesystem::path &path) {
  if (path.empty()) {
    throw std::invalid_argument("Path cannot be empty");
  }

  return std::filesystem::absolute(path).lexically_normal().string();
}
