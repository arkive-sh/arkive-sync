#include "platform/daemon/PollingDaemon.hpp"

#include "fs/FileWatcher.hpp"
#include "ipc/DaemonIpcHandler.hpp"
#include "ipc/DaemonIpcServer.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "service/QueueService.hpp"
#include "service/RemoteSyncService.hpp"
#include "service/SyncReconciler.hpp"
#include "service/SyncService.hpp"
#include "sync/RootScanner.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr int kMaxDirtyPathsPerRootPerTick = 100;

volatile std::sig_atomic_t gStopRequested = 0;

void handleStopSignal(int signal) {
  if (gStopRequested != 0) {
    std::_Exit(128 + signal);
  }

  gStopRequested = 1;
}

class ScopedSignalHandlers {
public:
  ScopedSignalHandlers()
      : previousSigint_(std::signal(SIGINT, handleStopSignal)),
        previousSigterm_(std::signal(SIGTERM, handleStopSignal)) {}

  ~ScopedSignalHandlers() {
    std::signal(SIGINT, previousSigint_);
    std::signal(SIGTERM, previousSigterm_);
  }

  ScopedSignalHandlers(const ScopedSignalHandlers &) = delete;
  ScopedSignalHandlers &operator=(const ScopedSignalHandlers &) = delete;

private:
  using SignalHandler = void (*)(int);

  SignalHandler previousSigint_;
  SignalHandler previousSigterm_;
};

std::vector<SyncRoot> remoteBoundRoots(const std::vector<SyncRoot> &syncRoots) {
  std::vector<SyncRoot> bound;
  for (const auto &root : syncRoots) {
    if (!root.folderId.empty()) {
      bound.push_back(root);
    }
  }
  return bound;
}

} // namespace

PollingDaemon::PollingDaemon(DaemonServices services)
    : services_(std::move(services)) {}

PollingDaemon::~PollingDaemon() = default;

int PollingDaemon::run() {
  gStopRequested = 0;
  ScopedSignalHandlers signalHandlers;
  DaemonIpcServer ipcServer(ipcEndpoint());
  ipcServer.start(makeDaemonIpcHandler(services_, [] { gStopRequested = 1; }));
  services_.queueRepo->retryRunning();
  auto roots = services_.syncService->getSyncRoots();

  if (services_.remoteSyncService != nullptr) {
    const auto boundRoots = remoteBoundRoots(roots);
    if (!boundRoots.empty()) {
      services_.remoteSyncService->runTick(boundRoots);
    }
    roots = services_.syncService->getSyncRoots();
  }

  for (const auto &root : roots) {
    if (!root.enabled) {
      continue;
    }

    if (!std::filesystem::exists(root.localPath)) {
      spdlog::info("Skipping local startup scan for missing sync root {} path {}",
                   root.Id, root.localPath);
      continue;
    }

    services_.watcher->addRoot(WatchRoot{
        .rootId = root.Id,
        .path = root.localPath,
    });

    if (!services_.rootScanner->scanRoot(root.Id)) {
      spdlog::error("Failed to start scan for sync root {}", root.Id);
      continue;
    }

    if (!services_.scanRepo->hasRunningScanJob(root.Id)) {
      services_.queueService->build(root.Id);
      if (services_.syncReconciler != nullptr) {
        services_.syncReconciler->reconcileRoot(root);
      }
    }
  }

  roots = services_.syncService->getSyncRoots();

  const auto processWatcherEvents = [&]() {
    for (const auto &fileEvent : services_.watcher->poll()) {
      if (fileEvent.oldPath.has_value()) {
        spdlog::debug("watch event type={} path={} old_path={}",
                      eventTypeName(fileEvent.type), fileEvent.path.string(),
                      fileEvent.oldPath->string());
      } else {
        spdlog::debug("watch event type={} path={}",
                      eventTypeName(fileEvent.type), fileEvent.path.string());
      }

      services_.dirtyPathRepo->record(fileEvent);
    }
  };

  while (!gStopRequested) {
    if (services_.remoteSyncService != nullptr) {
      const bool remoteScanned =
          services_.remoteSyncService->runTick(remoteBoundRoots(roots));
      if (remoteScanned && services_.syncReconciler != nullptr) {
        for (const auto &root : roots) {
          if (root.enabled) {
            services_.syncReconciler->reconcileRoot(root);
          }
        }
      }
    }

    processWatcherEvents();

    for (const auto &root : roots) {
      if (!root.enabled) {
        continue;
      }

      if (services_.scanRepo->hasRunningScanJob(root.Id)) {
        if (!services_.rootScanner->scanRoot(root.Id)) {
          spdlog::error("Failed to continue scan for sync root {}", root.Id);
        }
        if (!services_.scanRepo->hasRunningScanJob(root.Id)) {
          services_.queueService->build(root.Id);
          if (services_.syncReconciler != nullptr) {
            services_.syncReconciler->reconcileRoot(root);
          }
        }
        continue;
      }

      int processedDirtyPaths = 0;

      while (processedDirtyPaths < kMaxDirtyPathsPerRootPerTick) {
        const auto dirtyPath =
            services_.dirtyPathRepo->claimNextPending(root.Id);
        if (!dirtyPath.has_value()) {
          break;
        }

        processedDirtyPaths++;
        spdlog::debug("Claimed dirty path {} for root {}", dirtyPath->id,
                      root.Id);

        try {
          bool ok = false;

          switch (dirtyPath->eventType) {
          case DirtyPathEventType::Scan:
          case DirtyPathEventType::Delete:
            ok = dirtyPath->relativePath.has_value() &&
                 services_.rootScanner->scanPath(root.Id,
                                                 *dirtyPath->relativePath);
            if (!ok) {
              services_.dirtyPathRepo->markFailed(dirtyPath->id,
                                                  "scanPath failed");
              spdlog::error("Failed to scan dirty path {} for root {}",
                            dirtyPath->id, root.Id);
              break;
            }
            services_.queueService->build(root.Id);
            if (services_.syncReconciler != nullptr) {
              services_.syncReconciler->reconcileRoot(root);
            }
            services_.dirtyPathRepo->markDone(dirtyPath->id);
            break;
          case DirtyPathEventType::FullRescan:
            services_.scanRepo->ensureRunningScanJob(root.Id);
            services_.dirtyPathRepo->markDone(dirtyPath->id);
            break;
          }
        } catch (const std::exception &error) {
          services_.dirtyPathRepo->markFailed(dirtyPath->id, error.what());
          spdlog::error("Dirty path {} failed for root {}: {}", dirtyPath->id,
                        root.Id, error.what());
        } catch (...) {
          services_.dirtyPathRepo->markFailed(dirtyPath->id,
                                              "Unknown dirty path error");
          spdlog::error("Dirty path {} failed for root {} with unknown error",
                        dirtyPath->id, root.Id);
        }
      }
    }

    services_.queueService->runTick();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  services_.watcher->stop();
  ipcServer.stop();
  spdlog::info("Daemon stopped");
  return 0;
}
