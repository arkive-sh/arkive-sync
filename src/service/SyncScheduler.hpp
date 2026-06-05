#pragma once

#include "fs/FileWatcher.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

static constexpr int kDebounceWindowMS = 1000;
static constexpr int kMaxDelayMS = 20 * 1000;

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

  SyncScheduler(SyncRepo &syncRepo, SyncService &syncService,
                std::chrono::milliseconds debounceWindow =
                    std::chrono::milliseconds(kDebounceWindowMS),
                std::chrono::milliseconds maxDelay =
                    std::chrono::milliseconds(kMaxDelayMS));

  std::vector<WatchRoot> rootsToWatch() const;
  void enqueueEvent(const FileEvent &event);
  void enqueueFullRescan();
  int nextRunDelayMs() const;
  ScanResult runReadyScans();

private:
  struct RecentEvent {
    std::string rootId;
    std::filesystem::path path;
    FileEventType type;
    Clock::time_point seenAt{};
  };

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

  bool shouldSuppressEvent(const FileEvent &event, Clock::time_point now);
  void schedulePath(const std::string &rootId,
                    const std::filesystem::path &path, Clock::time_point now);
  void scheduleRoot(const std::string &rootId, Clock::time_point now);
  void clearScheduledPathsForRoot(const std::string &rootId);

  static constexpr auto kRecentEventWindow = std::chrono::milliseconds(250);
  static constexpr size_t kRecentEventBufferSize = 32;

  SyncRepo &syncRepo_;
  SyncService &syncService_;
  std::chrono::milliseconds debounceWindow_;
  std::chrono::milliseconds maxDelay_;
  std::array<std::optional<RecentEvent>, kRecentEventBufferSize> recentEvents_;
  size_t nextRecentEventSlot_{0};
  std::unordered_map<std::string, ScheduledRoot> scheduledRoots_;
  std::unordered_map<std::string, ScheduledPath> scheduledPaths_;
};
