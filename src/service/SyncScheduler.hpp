#pragma once

#include "fs/FileWatcher.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

class SyncRepo;
class SyncService;

class SyncScheduler {
public:
  using Clock = std::chrono::steady_clock;

  struct ScanResult {
    size_t scannedRoots{0};
    size_t changedEntries{0};
  };

  SyncScheduler(
      SyncRepo &syncRepo,
      SyncService &syncService,
      std::chrono::milliseconds debounceWindow = std::chrono::milliseconds(750));

  std::vector<WatchRoot> watchRoots() const;
  void schedule(const FileEvent &event);
  void scheduleAll();
  int nextWaitTimeoutMs() const;
  ScanResult runDueScans();

private:
  struct ScheduledRoot {
    std::filesystem::path path;
    bool pending{false};
    Clock::time_point dueAt{};
  };

  void scheduleRoot(const std::string &rootId, Clock::time_point now);

  SyncRepo &syncRepo_;
  SyncService &syncService_;
  std::chrono::milliseconds debounceWindow_;
  std::unordered_map<std::string, ScheduledRoot> scheduledRoots_;
};
