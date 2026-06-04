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
    size_t scannedPaths{0};
    size_t changedEntries{0};
  };

  SyncScheduler(
      SyncRepo &syncRepo,
      SyncService &syncService,
      std::chrono::milliseconds debounceWindow = std::chrono::milliseconds(750),
      std::chrono::milliseconds maxDelay = std::chrono::seconds(20));

  std::vector<WatchRoot> watchRoots() const;
  void schedule(const FileEvent &event);
  void scheduleAll();
  int nextWaitTimeoutMs() const;
  ScanResult runDueScans();

private:
  struct ScheduledRoot {
    std::filesystem::path path;
    bool pending{false};
    Clock::time_point firstEventAt{};
    Clock::time_point dueAt{};
  };

  struct ScheduledPath {
    std::string rootId;
    std::filesystem::path path;
    Clock::time_point firstSeenAt{};
    Clock::time_point dueAt{};
  };

  void schedulePath(const std::string &rootId, const std::filesystem::path &path,
                    Clock::time_point now);
  void scheduleRoot(const std::string &rootId, Clock::time_point now);
  void clearScheduledPathsForRoot(const std::string &rootId);

  SyncRepo &syncRepo_;
  SyncService &syncService_;
  std::chrono::milliseconds debounceWindow_;
  std::chrono::milliseconds maxDelay_;
  std::unordered_map<std::string, ScheduledRoot> scheduledRoots_;
  std::unordered_map<std::string, ScheduledPath> scheduledPaths_;
};
