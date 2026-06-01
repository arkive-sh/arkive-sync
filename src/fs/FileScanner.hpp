#pragma once
#include <filesystem>
#include <vector>

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

  std::vector<LocalEntry> scanFiles();

private:
  const std::filesystem::path path_;
};
