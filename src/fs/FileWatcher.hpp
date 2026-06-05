#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class FileEventType {
  Created,
  Modified,
  Deleted,
  MovedFrom,
  MovedTo,
  Renamed,
  AttributeChanged,
  Overflow,
  Unknown
};

static const char *eventTypeName(FileEventType type) {
  switch (type) {
  case FileEventType::Created:
    return "created";
  case FileEventType::Modified:
    return "modified";
  case FileEventType::Deleted:
    return "deleted";
  case FileEventType::MovedFrom:
    return "moved_from";
  case FileEventType::MovedTo:
    return "moved_to";
  case FileEventType::Renamed:
    return "renamed";
  case FileEventType::AttributeChanged:
    return "attrib";
  case FileEventType::Overflow:
    return "overflow";
  case FileEventType::Unknown:
    return "unknown";
  }

  return "unknown";
}

struct WatchRoot {
  std::string rootId;
  std::filesystem::path path;
};

struct FileEvent {
  std::string rootId;
  std::filesystem::path path;
  std::optional<std::filesystem::path> oldPath;
  FileEventType type;
  bool isDirectory{false};
  uint32_t cookie{0};
};

class IFileWatcher {
public:
  virtual ~IFileWatcher() = default;

  IFileWatcher() = default;
  IFileWatcher(const IFileWatcher &) = delete;
  IFileWatcher &operator=(const IFileWatcher &) = delete;
  IFileWatcher(IFileWatcher &&) = delete;
  IFileWatcher &operator=(IFileWatcher &&) = delete;

  virtual int fd() const = 0;
  virtual void addRoot(const WatchRoot &root) = 0;
  virtual std::vector<FileEvent> poll() = 0;
  virtual void stop() = 0;

  static std::unique_ptr<IFileWatcher> create();
};
