#include "./RootScanner.hpp"
#include "fs/FileWatcher.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncService.hpp"
#include <filesystem>
#include <memory>
#include <optional>

RootScanner::RootScanner(SyncService &syncSvc)
    : watcher_(IFileWatcher::create()), syncSvc_(syncSvc) {}

RootScanner::RootScanner(SyncService &syncSvc,
                         std::unique_ptr<IFileWatcher> watcher)
    : watcher_(std::move(watcher)), syncSvc_(syncSvc) {}

bool RootScanner::scanRoot(const std::string &syncRootId) {
  std::optional<SyncRoot> syncRoot = syncSvc_.findSyncRootById(syncRootId);
  if (!syncRoot.has_value()) {
    return false;
  }

  if (!syncRoot->enabled) {
    return false;
  }

  std::filesystem::path rootAbsPath =
      std::filesystem::absolute(syncRoot->localPath);

  if (!rootAbsPath.is_absolute() ||
      !std::filesystem::is_directory(rootAbsPath)) {
    return false;
  }

  // Now we need to start scan
}
