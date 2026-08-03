#pragma once

#include "fs/FileWatcher.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct FileSnapshotEntry {
  std::filesystem::path path;
  std::string identity;
  std::uintmax_t size{0};
  std::filesystem::file_time_type modifiedAt{};
  bool isDirectory{false};
};

using FileSnapshot = std::unordered_map<std::string, FileSnapshotEntry>;

FileSnapshot takeFileSnapshot(const std::filesystem::path &root);

std::vector<FileEvent> diffFileSnapshots(const std::string &rootId,
                                         const FileSnapshot &before,
                                         const FileSnapshot &after);
