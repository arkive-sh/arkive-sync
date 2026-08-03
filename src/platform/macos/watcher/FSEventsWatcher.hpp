#pragma once

#include "fs/FileSnapshot.hpp"
#include "fs/FileWatcher.hpp"

#include <CoreServices/CoreServices.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

class FSEventsWatcher final : public IFileWatcher {
public:
  struct RootState {
    std::string rootId;
    std::filesystem::path path;
    FileSnapshot snapshot;
    FSEventStreamRef stream{nullptr};
    CFRunLoopRef runLoop{nullptr};
    std::thread thread;
    std::atomic_bool dirty{false};
  };

  FSEventsWatcher() = default;
  ~FSEventsWatcher() override;

  int fd() const override { return -1; }
  void addRoot(const WatchRoot &root) override;
  std::vector<FileEvent> poll() override;
  void stop() override;

private:
  std::mutex mutex_;
  std::vector<std::unique_ptr<RootState>> roots_;
  bool stopped_{false};
};
