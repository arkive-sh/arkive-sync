#pragma once

#include "fs/FileSnapshot.hpp"
#include "fs/FileWatcher.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

class ReadDirectoryChangesWatcher final : public IFileWatcher {
public:
  struct RootState {
    std::string rootId;
    std::filesystem::path path;
    FileSnapshot snapshot;
    HANDLE dir{INVALID_HANDLE_VALUE};
    std::thread thread;
    std::atomic_bool running{true};
    std::atomic_bool dirty{false};
  };

  ReadDirectoryChangesWatcher() = default;
  ~ReadDirectoryChangesWatcher() override;

  int fd() const override { return -1; }
  void addRoot(const WatchRoot &root) override;
  std::vector<FileEvent> poll() override;
  void stop() override;

private:
  std::mutex mutex_;
  std::vector<std::unique_ptr<RootState>> roots_;
  bool stopped_{false};
};
