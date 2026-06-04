#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct WatchRoot {
  std::string rootId;
  std::filesystem::path path;
};

enum class FileEventType {
  Created,
  Modified,
  Deleted,
  Moved,
  AttributeChanged,
  Overflow,
  Unknown
};

struct FileEvent {
  std::string rootId;
  std::filesystem::path path;
  FileEventType type;
  bool isDirectory{false};
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
