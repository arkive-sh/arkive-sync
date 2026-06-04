#include "service/SyncScheduler.hpp"

#include "repo/SyncRepo.hpp"
#include "service/SyncService.hpp"

#include <algorithm>
#include <optional>

SyncScheduler::SyncScheduler(SyncRepo &syncRepo, SyncService &syncService,
                             std::chrono::milliseconds debounceWindow)
    : syncRepo_(syncRepo), syncService_(syncService),
      debounceWindow_(debounceWindow) {
  for (const auto &syncRoot : syncRepo_.getSyncRoots()) {
    if (!syncRoot.enabled) {
      continue;
    }

    scheduledRoots_[syncRoot.id] = ScheduledRoot{
        .path = syncRoot.localPath,
        .pending = false,
        .dueAt = Clock::time_point{},
    };
  }
}

std::vector<WatchRoot> SyncScheduler::watchRoots() const {
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

void SyncScheduler::schedule(const FileEvent &event) {
  if (event.type == FileEventType::Overflow || event.rootId.empty()) {
    scheduleAll();
    return;
  }

  scheduleRoot(event.rootId, Clock::now());
}

void SyncScheduler::scheduleAll() {
  const Clock::time_point now = Clock::now();
  for (const auto &[rootId, _] : scheduledRoots_) {
    scheduleRoot(rootId, now);
  }
}

int SyncScheduler::nextWaitTimeoutMs() const {
  std::optional<Clock::time_point> nextDueAt;

  for (const auto &[_, root] : scheduledRoots_) {
    if (!root.pending) {
      continue;
    }

    if (!nextDueAt.has_value() || root.dueAt < *nextDueAt) {
      nextDueAt = root.dueAt;
    }
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

SyncScheduler::ScanResult SyncScheduler::runDueScans() {
  const Clock::time_point now = Clock::now();
  ScanResult result;

  for (auto &[_, root] : scheduledRoots_) {
    if (!root.pending || root.dueAt > now) {
      continue;
    }

    result.changedEntries += syncService_.scanRoot(root.path);
    result.scannedRoots += 1;
    root.pending = false;
  }

  return result;
}

void SyncScheduler::scheduleRoot(const std::string &rootId, Clock::time_point now) {
  auto it = scheduledRoots_.find(rootId);
  if (it == scheduledRoots_.end()) {
    return;
  }

  it->second.pending = true;
  it->second.dueAt = now + debounceWindow_;
}
