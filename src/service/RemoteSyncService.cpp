#include "service/RemoteSyncService.hpp"

#include "fs/helpers/PathHelpers.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "sync/RemoteScanner.hpp"

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace {

constexpr auto kRemoteScanInterval = std::chrono::seconds(30);

} // namespace

RemoteSyncService::RemoteSyncService(
    std::unique_ptr<RemoteScanner> remoteScanner,
    RemoteEntryRepo &remoteEntryRepo,
    SyncRepo &syncRepo)
    : remoteScanner_(std::move(remoteScanner)),
      remoteEntryRepo_(remoteEntryRepo),
      syncRepo_(syncRepo) {}

RemoteSyncService::~RemoteSyncService() = default;

bool RemoteSyncService::runTick(const std::vector<SyncRoot> &roots) {
  if (remoteScanner_ == nullptr) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  bool scanned = false;

  for (const auto &root : roots) {
    if (!root.enabled) {
      continue;
    }

    const auto it = lastRemoteScanAt_.find(root.Id);
    if (it != lastRemoteScanAt_.end() &&
        now - it->second < kRemoteScanInterval) {
      continue;
    }

    remoteScanner_->scanRoot(root.Id);
    scanned = true;
    if (remoteScanner_->isRootDeleted(root.Id)) {
      remoteEntryRepo_.markRootRemoteDeleted(root.Id);
      syncRepo_.disableSyncRoot(root.Id);
      std::error_code error;
      std::filesystem::remove_all(normalizeFsPath(root.localPath), error);
      if (error) {
        spdlog::error("Failed to delete remote-deleted sync root {}: {}",
                      root.localPath, error.message());
      }
      spdlog::info("Disabled sync root {} after remote deletion", root.Id);
    }
    lastRemoteScanAt_[root.Id] = now;
  }

  return scanned;
}
