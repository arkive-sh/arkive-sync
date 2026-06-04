#include "./FileScanner.hpp"
#include <filesystem>
#include <system_error>
#include <stdexcept>

namespace fs = std::filesystem;

FileScanner::FileScanner(const fs::path &fsPath) : path_(fsPath) {}

const fs::path &FileScanner::rootPath() const { return path_; }

void FileScanner::scanFiles(const std::function<void(const LocalEntry &)> &onEntry) {
  if (!fs::exists(path_) || !fs::is_directory(path_)) {
    throw std::invalid_argument("Invalid. The path needs to be a folder");
  }

  fs::directory_options options = fs::directory_options::skip_permission_denied;
  for (const auto &entry : fs::recursive_directory_iterator(path_, options)) {
    std::error_code error;
    const fs::file_status status = entry.symlink_status(error);
    if (error) {
      continue;
    }

    const bool isDirectory = fs::is_directory(status);
    const bool isRegularFile = fs::is_regular_file(status);
    if (!isDirectory && !isRegularFile) {
      continue;
    }

    const uint64_t size = isRegularFile ? entry.file_size(error) : 0;
    if (error) {
      continue;
    }

    const auto modifiedTime = entry.last_write_time(error);
    if (error) {
      continue;
    }

    onEntry({
        .absolutePath = fs::absolute(entry.path()),
        .relativePath = entry.path().lexically_relative(path_),
        .size = size,
        .modifiedTime = modifiedTime,
        .isDirectory = isDirectory,
    });
  }
}
