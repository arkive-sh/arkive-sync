#pragma once
#include <functional>
#include <filesystem>

struct LocalEntry {
  std::filesystem::path absolutePath;
  std::filesystem::path relativePath;

  uint64_t size;
  std::filesystem::file_time_type modifiedTime;

  bool isDirectory;
};

class FileScanner {
public:
  explicit FileScanner(const std::filesystem::path &path);

  // Avoid copying
  FileScanner(const FileScanner &) = delete;
  FileScanner &operator=(const FileScanner &) = delete;

  const std::filesystem::path &rootPath() const;
  void scanFiles(const std::function<void(const LocalEntry &)> &onEntry);

private:
  const std::filesystem::path path_;
};
