#include "service/SyncScheduler.hpp"

#include "repo/SyncRepo.hpp"

#include <algorithm>
#include <optional>
#include <spdlog/spdlog.h>

SyncScheduler::SyncScheduler(SyncRepo &syncRepo,
                             std::chrono::milliseconds debounceWindow,
                             std::chrono::milliseconds maxDelay)
    : syncRepo_(syncRepo), debounceWindow_(debounceWindow),
      maxDelay_(maxDelay) {
  for (const auto &syncRoot : syncRepo_.getSyncRoots()) {
    if (!syncRoot.enabled) {
      continue;
    }

    scheduledRoots_[syncRoot.id] = ScheduledRoot{
        .path = syncRoot.localPath,
        .pending = false,
        .firstEventAt = Clock::time_point{},
        .dueAt = Clock::time_point{},
    };
  }
}

std::vector<WatchRoot> SyncScheduler::rootsToWatch() const {
  std::vector<WatchRoot> roots;
  roots.reserve(scheduledRoots_.size());

  for (const auto &[rootId, root] : scheduledRoots_) {
    roots.push_back(WatchRoot{
        .rootId = rootId,
        .path = root.path,
    });
  }

  return roots;
}

void SyncScheduler::enqueueEvent(const FileEvent &event) {
  const Clock::time_point now = Clock::now();
  if (shouldSuppressEvent(event, now)) {
    spdlog::info("Scheduler suppressed duplicate event {} root={} path={}",
                 eventTypeName(event.type), event.rootId, event.path.string());
    return;
  }

  spdlog::info("Scheduler received event {} root={} path={} old={}",
               eventTypeName(event.type), event.rootId, event.path.string(),
               event.oldPath.has_value() ? event.oldPath->string() : "-");

  if (event.rootId.empty()) {
    enqueueFullRescan();
    return;
  }

  if (event.type == FileEventType::Overflow ||
      event.type == FileEventType::Unknown) {
    spdlog::info("Scheduler queued full root scan for root={}", event.rootId);
    scheduleRoot(event.rootId, now);
    return;
  }

  if (event.type == FileEventType::Renamed && event.oldPath.has_value()) {
    spdlog::info("Scheduler queued rename scans root={} old={} new={}",
                 event.rootId, event.oldPath->string(), event.path.string());
    schedulePath(event.rootId, *event.oldPath, now);
    schedulePath(event.rootId, event.path, now);
    return;
  }

  spdlog::info("Scheduler queued path scan root={} path={}", event.rootId,
               event.path.string());
  schedulePath(event.rootId, event.path, now);
}

bool SyncScheduler::shouldSuppressEvent(const FileEvent &event,
                                        Clock::time_point now) {
  if (event.type != FileEventType::Deleted) {
    return false;
  }

  const std::filesystem::path normalizedPath =
      event.path.empty()
          ? std::filesystem::path{}
          : std::filesystem::absolute(event.path).lexically_normal();

  for (const auto &recentEventSlot : recentEvents_) {
    if (!recentEventSlot.has_value()) {
      continue;
    }

    const RecentEvent &recentEvent = *recentEventSlot;
    if (now - recentEvent.seenAt > kRecentEventWindow) {
      continue;
    }

    if (recentEvent.rootId == event.rootId && recentEvent.path == normalizedPath &&
        recentEvent.type == event.type) {
      return true;
    }
  }

  recentEvents_[nextRecentEventSlot_] = RecentEvent{
      .rootId = event.rootId,
      .path = normalizedPath,
      .type = event.type,
      .seenAt = now,
  };
  nextRecentEventSlot_ = (nextRecentEventSlot_ + 1) % recentEvents_.size();
  return false;
}

void SyncScheduler::enqueueFullRescan() {
  const Clock::time_point now = Clock::now();
  for (const auto &[rootId, _] : scheduledRoots_) {
    scheduleRoot(rootId, now);
  }
}

int SyncScheduler::nextRunDelayMs() const {
  std::optional<Clock::time_point> nextDueAt;

  auto updateNextDueAt = [&](Clock::time_point dueAt) {
    if (!nextDueAt.has_value() || dueAt < *nextDueAt) {
      nextDueAt = dueAt;
    }
  };

  for (const auto &[_, root] : scheduledRoots_) {
    if (root.pending) {
      updateNextDueAt(root.dueAt);
    }
  }

  for (const auto &[_, path] : scheduledPaths_) {
    updateNextDueAt(path.dueAt);
  }

  if (!nextDueAt.has_value()) {
    return -1;
  }

  const Clock::time_point now = Clock::now();
  if (*nextDueAt <= now) {
    return 0;
  }

  const auto waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
      *nextDueAt - now);
  return std::max(1, static_cast<int>(waitDuration.count()));
}

std::vector<SyncScheduler::ScanJob> SyncScheduler::drainDueJobs() {
  const Clock::time_point now = Clock::now();
  std::vector<ScanJob> jobs;

  for (auto &[rootId, root] : scheduledRoots_) {
    if (!root.pending || root.dueAt > now) {
      continue;
    }

    spdlog::info("Scheduler draining root job root={} path={}", rootId,
                 root.path.string());
    jobs.push_back(ScanJob{
        .type = ScanJobType::Root,
        .rootId = rootId,
        .path = root.path,
    });
    root.pending = false;
    root.firstEventAt = Clock::time_point{};
    root.dueAt = Clock::time_point{};
    clearScheduledPathsForRoot(rootId);
  }

  for (auto it = scheduledPaths_.begin(); it != scheduledPaths_.end();) {
    if (it->second.dueAt > now) {
      ++it;
      continue;
    }

    spdlog::info("Scheduler draining path job root={} path={}",
                 it->second.rootId, it->second.path.string());
    jobs.push_back(ScanJob{
        .type = ScanJobType::Path,
        .rootId = it->second.rootId,
        .path = it->second.path,
    });
    it = scheduledPaths_.erase(it);
  }

  return jobs;
}

void SyncScheduler::schedulePath(const std::string &rootId,
                                 const std::filesystem::path &path,
                                 Clock::time_point now) {
  const auto rootIt = scheduledRoots_.find(rootId);
  if (rootIt == scheduledRoots_.end() || rootIt->second.pending) {
    return;
  }

  const std::filesystem::path normalizedPath =
      std::filesystem::absolute(path).lexically_normal();
  const std::string pathKey = rootId + "\n" + normalizedPath.string();
  auto [it, inserted] = scheduledPaths_.try_emplace(
      pathKey, ScheduledPath{
                   .rootId = rootId,
                   .path = normalizedPath,
                   .firstSeenAt = now,
                   .dueAt = Clock::time_point{},
               });

  if (inserted) {
    it->second.firstSeenAt = now;
  }

  const Clock::time_point maxDueAt = it->second.firstSeenAt + maxDelay_;
  const Clock::time_point debounceDueAt = now + debounceWindow_;
  it->second.dueAt = std::min(debounceDueAt, maxDueAt);
}

void SyncScheduler::scheduleRoot(const std::string &rootId, Clock::time_point now) {
  auto it = scheduledRoots_.find(rootId);
  if (it == scheduledRoots_.end()) {
    return;
  }

  if (!it->second.pending) {
    it->second.firstEventAt = now;
  }

  const Clock::time_point maxDueAt = it->second.firstEventAt + maxDelay_;
  const Clock::time_point debounceDueAt = now + debounceWindow_;
  it->second.pending = true;
  it->second.dueAt = std::min(debounceDueAt, maxDueAt);
  clearScheduledPathsForRoot(rootId);
}

void SyncScheduler::clearScheduledPathsForRoot(const std::string &rootId) {
  for (auto it = scheduledPaths_.begin(); it != scheduledPaths_.end();) {
    if (it->second.rootId == rootId) {
      it = scheduledPaths_.erase(it);
      continue;
    }
    ++it;
  }
}
