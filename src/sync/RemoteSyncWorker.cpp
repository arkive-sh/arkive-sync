#include "sync/RemoteSyncWorker.hpp"

#include "repo/SyncRepo.hpp"
#include "sync/Reconcile.hpp"
#include "sync/ReconcileActionApplier.hpp"
#include "sync/RemoteScanner.hpp"

#include <chrono>
#include <spdlog/spdlog.h>

namespace {

constexpr auto kRemoteScanInterval = std::chrono::seconds(60);

}

RemoteSyncWorker::RemoteSyncWorker(RemoteScanner &scanner, SyncRepo &syncRepo)
    : scanner_(scanner), syncRepo_(syncRepo) {}

RemoteSyncWorker::~RemoteSyncWorker() { stop(); }

void RemoteSyncWorker::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    triggered_ = true;
  }

  worker_ = std::thread(&RemoteSyncWorker::runLoop, this);
  cv_.notify_one();
}

void RemoteSyncWorker::stop() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false)) {
    return;
  }

  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void RemoteSyncWorker::trigger() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    triggered_ = true;
  }

  cv_.notify_one();
}

void RemoteSyncWorker::runLoop() {
  while (running_.load()) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, kRemoteScanInterval,
                   [&] { return !running_.load() || triggered_; });

      if (!running_.load()) {
        return;
      }

      triggered_ = false;
    }

    try {
      spdlog::info("RemoteSyncWorker running remote scan");
      scanner_.scanAllRootsAndStore(true);
      spdlog::info("RemoteSyncWorker building reconcile plan");
      ReconcileEngine reconcile(syncRepo_.local());
      ReconcileActionApplier applier(syncRepo_.local());

      // Later this will be driven dynamically
      const SyncModeSpec *mode = findSyncMode(SyncMode::RemoteMirror);

      for (const auto &syncRoot : syncRepo_.roots().getSyncRoots()) {
        if (!syncRoot.enabled) {
          continue;
        }

        const ReconcilePlan plan = reconcile.plan(syncRoot.id, *mode);

        for (const auto &action : plan.actions) {
          spdlog::info(
              "reconcile planned action={} root={} entry={} path={} reason={}",
              action.type == ReconcileActionType::ApplyRemoteDeleteFolder
                  ? "delete_local_folder"
                  : "delete_local_file",
              action.syncRootId, action.entryId, action.localPath,
              action.reason);
        }

        applier.apply(syncRoot, plan);
      }

      spdlog::info("RemoteSyncWorker finished remote scan");

    } catch (const std::exception &ex) {
      spdlog::error("RemoteSyncWorker failed: {}", ex.what());
    }
  }
}
