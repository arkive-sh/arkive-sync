#include "service/RemoteSyncService.hpp"

#include "fs/helpers/PathHelpers.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "sync/RemoteScanner.hpp"

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace {

constexpr auto kRemoteScanInterval = std::chrono::seconds(30);

} // namespace

RemoteSyncService::RemoteSyncService(EntryRepo &entryRepo,
                                     std::unique_ptr<RemoteScanner> remoteScanner)
    : entryRepo_(entryRepo), remoteScanner_(std::move(remoteScanner)) {}

RemoteSyncService::~RemoteSyncService() = default;

void RemoteSyncService::runTick(const std::vector<SyncRoot> &roots) {
  if (remoteScanner_ == nullptr) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();

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
    reconcileDeletedEntries(root);
    lastRemoteScanAt_[root.Id] = now;
  }
}

void RemoteSyncService::reconcileDeletedEntries(const SyncRoot &root) {
  for (const auto &entry : entryRepo_.listRemoteDeletedEntriesBySyncRootId(
           root.Id)) {
    const std::filesystem::path absolutePath =
        std::filesystem::path(normalizeFsPath(root.localPath)) /
        entry.relativePath;
    std::error_code error;
    if (entry.isDirectory) {
      std::filesystem::remove_all(absolutePath, error);
    } else {
      std::filesystem::remove(absolutePath, error);
    }
    if (error) {
      spdlog::error("Failed to delete remote-deleted path {} for root {}: {}",
                    absolutePath.string(), root.Id, error.message());
    }
  }
}
