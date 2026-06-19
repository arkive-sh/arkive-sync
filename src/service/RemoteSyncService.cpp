#include "service/RemoteSyncService.hpp"

#include "fs/helpers/PathHelpers.hpp"
#include "repo/SyncRepo.hpp"
#include "sync/RemoteScanner.hpp"

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace {

constexpr auto kRemoteScanInterval = std::chrono::seconds(30);

} // namespace

RemoteSyncService::RemoteSyncService(
    std::unique_ptr<RemoteScanner> remoteScanner)
    : remoteScanner_(std::move(remoteScanner)) {}

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
    if (remoteScanner_->isRootDeleted(root.Id)) {
      std::error_code error;
      std::filesystem::remove_all(normalizeFsPath(root.localPath), error);
      if (error) {
        spdlog::error("Failed to delete remote-deleted sync root {}: {}",
                      root.localPath, error.message());
      }
    }
    lastRemoteScanAt_[root.Id] = now;
  }
}
