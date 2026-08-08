#include "platform/linux/daemon/LinuxDaemon.hpp"

#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "fs/FileWatcher.hpp"
#include "fs/FileEncryptor.hpp"
#include "platform/AppDataPaths.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "repo/UserRepo.hpp"
#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
#include "ipc/DaemonIpcHandler.hpp"
#include "ipc/DaemonIpcServer.hpp"
#include "download/DownloadRecordDecryptor.hpp"
#include "download/DownloadService.hpp"
#include "repo/ConflictRepo.hpp"
#include "repo/LocalEntryRepo.hpp"
#include "repo/RemoteEntryRepo.hpp"
#include "service/FolderCreateWorker.hpp"
#include "service/QueueService.hpp"
#include "service/RemoteSyncService.hpp"
#include "service/SyncReconciler.hpp"
#include "service/SyncService.hpp"
#include "service/UploadJobRunner.hpp"
#include "service/UploadService.hpp"
#include "service/VaultService.hpp"
#include "sync/RootScanner.hpp"

#include <cerrno>
#include <csignal>
#include <optional>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr int kMaxDirtyPathsPerRootPerTick = 100;

volatile std::sig_atomic_t gStopRequested = 0;

void handleStopSignal(int signal) {
  if (gStopRequested != 0) {
    _exit(128 + signal);
  }

  gStopRequested = 1;
}

class ScopedFd {
public:
  explicit ScopedFd(int fd) : fd_(fd) {}

  ~ScopedFd() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  ScopedFd(const ScopedFd &) = delete;
  ScopedFd &operator=(const ScopedFd &) = delete;

  int get() const { return fd_; }

private:
  int fd_{-1};
};

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

} // namespace

LinuxDaemon::LinuxDaemon(DaemonServices services)
    : services_(std::move(services)) {}

LinuxDaemon::~LinuxDaemon() = default;

int LinuxDaemon::run() {
  gStopRequested = 0;
  ScopedSignalHandlers signalHandlers;
  DaemonIpcServer ipcServer(ipcEndpoint());
  ipcServer.start(makeDaemonIpcHandler(services_, [] { gStopRequested = 1; }));
  services_.queueRepo->retryRunning();
  auto roots = services_.syncService->getSyncRoots();

  const auto remoteBoundRoots = [](
      const std::vector<SyncRoot> &syncRoots) {
    std::vector<SyncRoot> bound;
    for (const auto &root : syncRoots) {
      if (!root.folderId.empty()) {
        bound.push_back(root);
      }
    }
    return bound;
  };

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

  }

  roots = services_.syncService->getSyncRoots();

  std::unordered_map<std::string, std::optional<std::string>> queueCursors;
  std::unordered_map<std::string, bool> queueComplete;
  for (const auto &root : roots) {
    queueComplete[root.Id] = false;
  }

  const auto produceQueuePage = [&](const SyncRoot &root,
                                    bool scanComplete) {
    if (queueComplete[root.Id]) {
      return;
    }

    const auto cursor = queueCursors.find(root.Id);
    const QueueBuildPage page = services_.queueService->buildPage(
        root.Id, cursor == queueCursors.end() ? std::nullopt : cursor->second,
        scanComplete);
    if (page.nextPath.has_value()) {
      queueCursors[root.Id] = page.nextPath;
    }
    if (page.complete) {
      queueComplete[root.Id] = true;
      queueCursors.erase(root.Id);
    }
  };

  for (const auto &root : roots) {
    if (!root.enabled) {
      continue;
    }
    const bool scanComplete =
        !services_.scanRepo->hasRunningScanJob(root.Id);
    produceQueuePage(root, scanComplete);
    if (scanComplete && services_.syncReconciler != nullptr) {
      services_.syncReconciler->reconcileRoot(root);
    }
  }

  const ScopedFd epollFd(epoll_create1(EPOLL_CLOEXEC));
  if (epollFd.get() < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_create1 failed");
  }

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = services_.watcher->fd();

  if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, services_.watcher->fd(),
                &event) < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_ctl add watcher failed");
  }

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
    for (const auto &root : roots) {
      if (!root.enabled) {
        continue;
      }

      if (services_.scanRepo->hasRunningScanJob(root.Id)) {
        if (!services_.rootScanner->scanRoot(root.Id)) {
          spdlog::error("Failed to continue scan for sync root {}", root.Id);
        }
        const bool scanComplete =
            !services_.scanRepo->hasRunningScanJob(root.Id);
        produceQueuePage(root, scanComplete);
        if (scanComplete && services_.syncReconciler != nullptr) {
          services_.syncReconciler->reconcileRoot(root);
        }
        continue;
      }

      produceQueuePage(root, true);

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
            services_.queueService->enqueueEntry(root.Id,
                                                 *dirtyPath->relativePath);
            if (services_.syncReconciler != nullptr) {
              services_.syncReconciler->reconcileRoot(root);
            }
            services_.dirtyPathRepo->markDone(dirtyPath->id);
            break;
          case DirtyPathEventType::FullRescan:
            services_.scanRepo->ensureRunningScanJob(root.Id);
            queueCursors.erase(root.Id);
            queueComplete[root.Id] = false;
            services_.dirtyPathRepo->markDone(dirtyPath->id);
            break;
          }
        } catch (const std::exception &error) {
          services_.dirtyPathRepo->markFailed(dirtyPath->id, error.what());
          spdlog::error("Dirty path {} failed for root {}: {}", dirtyPath->id,
                        root.Id, error.what());
        } catch (...) {
          services_.dirtyPathRepo->markFailed(dirtyPath->id,
                                     "Unknown dirty path processing error");
          spdlog::error("Dirty path {} failed for root {} with unknown error",
                        dirtyPath->id, root.Id);
        }
      }
    }

    services_.queueService->runTick();

    epoll_event readyEvents[8]{};
    const int readyCount = epoll_wait(epollFd.get(), readyEvents, 8, 1000);

    if (readyCount < 0) {
      if (errno == EINTR) {
        continue;
      }

      throw std::system_error(errno, std::generic_category(),
                              "epoll_wait failed");
    }

    // poll every tick so expired move/delete events are flushed
    // even when no new inotify fd readability arrives.
    if (readyCount == 0) {
      processWatcherEvents();
      continue;
    }

    for (int i = 0; i < readyCount; ++i) {
      if (readyEvents[i].data.fd != services_.watcher->fd()) {
        continue;
      }

      processWatcherEvents();
    }
  }

  services_.watcher->stop();
  ipcServer.stop();
  spdlog::info("Daemon stopped");
  return 0;
}
