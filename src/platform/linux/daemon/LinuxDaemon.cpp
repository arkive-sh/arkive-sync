#include "platform/linux/daemon/LinuxDaemon.hpp"

#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "fs/FileWatcher.hpp"
#include "fs/FileEncryptor.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/QueueRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "repo/UploadResumeRepo.hpp"
#include "repo/UserRepo.hpp"
#include "api/ArkiveApi.hpp"
#include "api/ArkiveHttpClient.hpp"
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
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <system_error>
#include <unistd.h>

namespace {

constexpr int kMaxDirtyPathsPerRootPerTick = 100;

volatile std::sig_atomic_t gStopRequested = 0;

void handleStopSignal(int) { gStopRequested = 1; }

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

LinuxDaemon::LinuxDaemon(std::unique_ptr<Database> db,
                         std::unique_ptr<RustCrypto> crypto,
                         std::unique_ptr<SyncRepo> syncRepo,
                         std::unique_ptr<ScanRepo> scanRepo,
                         std::unique_ptr<DirtyPathRepo> dirtyPathRepo,
                         std::unique_ptr<EntryRepo> entryRepo,
                         std::unique_ptr<LocalEntryRepo> localEntryRepo,
                         std::unique_ptr<RemoteEntryRepo> remoteEntryRepo,
                         std::unique_ptr<ConflictRepo> conflictRepo,
                         std::unique_ptr<QueueRepo> queueRepo,
                         std::unique_ptr<QueueService> queueService,
                         std::unique_ptr<RemoteSyncService> remoteSyncService,
                         std::unique_ptr<UserRepo> userRepo,
                         std::unique_ptr<UploadResumeRepo> uploadResumeRepo,
                         std::unique_ptr<VaultService> vaultService,
                         std::unique_ptr<FileEncryptor> fileEncryptor,
                         std::unique_ptr<ArkiveHttpClient> client,
                         std::unique_ptr<ArkiveApi> api,
                         std::unique_ptr<FolderCreateWorker> folderCreateWorker,
                         std::unique_ptr<UploadService> uploadService,
                         std::unique_ptr<UploadJobRunner> uploadJobRunner,
                         std::unique_ptr<DownloadRecordDecryptor>
                             downloadRecordDecryptor,
                         std::unique_ptr<DownloadService> downloadService,
                         std::unique_ptr<SyncService> syncService,
                         std::unique_ptr<SyncReconciler> syncReconciler,
                         std::unique_ptr<RootScanner> rootScanner,
                         std::unique_ptr<IFileWatcher> watcher)
    : db_(std::move(db)), crypto_(std::move(crypto)),
      syncRepo_(std::move(syncRepo)), scanRepo_(std::move(scanRepo)),
      dirtyPathRepo_(std::move(dirtyPathRepo)),
      entryRepo_(std::move(entryRepo)),
      localEntryRepo_(std::move(localEntryRepo)),
      remoteEntryRepo_(std::move(remoteEntryRepo)),
      conflictRepo_(std::move(conflictRepo)),
      queueRepo_(std::move(queueRepo)),
      queueService_(std::move(queueService)),
      remoteSyncService_(std::move(remoteSyncService)),
      userRepo_(std::move(userRepo)),
      uploadResumeRepo_(std::move(uploadResumeRepo)),
      vaultService_(std::move(vaultService)),
      fileEncryptor_(std::move(fileEncryptor)), client_(std::move(client)),
      api_(std::move(api)),
      folderCreateWorker_(std::move(folderCreateWorker)),
      uploadService_(std::move(uploadService)),
      uploadJobRunner_(std::move(uploadJobRunner)),
      downloadRecordDecryptor_(std::move(downloadRecordDecryptor)),
      downloadService_(std::move(downloadService)),
      syncService_(std::move(syncService)),
      syncReconciler_(std::move(syncReconciler)),
      rootScanner_(std::move(rootScanner)),
      watcher_(std::move(watcher)) {}

LinuxDaemon::~LinuxDaemon() = default;

int LinuxDaemon::run() {
  gStopRequested = 0;
  ScopedSignalHandlers signalHandlers;
  const auto roots = syncService_->getSyncRoots();

  for (const auto &root : roots) {
    if (!root.enabled) {
      continue;
    }

    if (!std::filesystem::exists(root.localPath)) {
      spdlog::info("Skipping local startup scan for missing sync root {} path {}",
                   root.Id, root.localPath);
      continue;
    }

    watcher_->addRoot(WatchRoot{
        .rootId = root.Id,
        .path = root.localPath,
    });

    if (!rootScanner_->scanRoot(root.Id)) {
      spdlog::error("Failed to start scan for sync root {}", root.Id);
      continue;
    }

    queueService_->build(root.Id);
    if (syncReconciler_ != nullptr) {
      syncReconciler_->reconcileRoot(root);
    }
  }

  const ScopedFd epollFd(epoll_create1(EPOLL_CLOEXEC));
  if (epollFd.get() < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_create1 failed");
  }

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = watcher_->fd();

  if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, watcher_->fd(), &event) < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_ctl add watcher failed");
  }

  const auto processWatcherEvents = [&]() {
    for (const auto &fileEvent : watcher_->poll()) {
      if (fileEvent.oldPath.has_value()) {
        spdlog::debug("watch event type={} path={} old_path={}",
                      eventTypeName(fileEvent.type), fileEvent.path.string(),
                      fileEvent.oldPath->string());
      } else {
        spdlog::debug("watch event type={} path={}",
                      eventTypeName(fileEvent.type), fileEvent.path.string());
      }

      dirtyPathRepo_->record(fileEvent);
    }
  };

  while (!gStopRequested) {
    if (remoteSyncService_ != nullptr) {
      const bool remoteScanned = remoteSyncService_->runTick(roots);
      if (remoteScanned && syncReconciler_ != nullptr) {
        for (const auto &root : roots) {
          if (root.enabled) {
            syncReconciler_->reconcileRoot(root);
          }
        }
      }
    }
    for (const auto &root : roots) {
      if (!root.enabled) {
        continue;
      }

      if (scanRepo_->hasRunningScanJob(root.Id)) {
        if (!rootScanner_->scanRoot(root.Id)) {
          spdlog::error("Failed to continue scan for sync root {}", root.Id);
        }
        queueService_->build(root.Id);
        if (syncReconciler_ != nullptr) {
          syncReconciler_->reconcileRoot(root);
        }
        continue;
      }

      int processedDirtyPaths = 0;

      while (processedDirtyPaths < kMaxDirtyPathsPerRootPerTick) {
        const auto dirtyPath = dirtyPathRepo_->claimNextPending(root.Id);
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
                 rootScanner_->scanPath(root.Id, *dirtyPath->relativePath);
            if (!ok) {
              dirtyPathRepo_->markFailed(dirtyPath->id, "scanPath failed");
              spdlog::error("Failed to scan dirty path {} for root {}",
                            dirtyPath->id, root.Id);
              break;
            }
            queueService_->build(root.Id);
            if (syncReconciler_ != nullptr) {
              syncReconciler_->reconcileRoot(root);
            }
            dirtyPathRepo_->markDone(dirtyPath->id);
            break;
          case DirtyPathEventType::FullRescan:
            scanRepo_->ensureRunningScanJob(root.Id);
            dirtyPathRepo_->markDone(dirtyPath->id);
            break;
          }
        } catch (const std::exception &error) {
          dirtyPathRepo_->markFailed(dirtyPath->id, error.what());
          spdlog::error("Dirty path {} failed for root {}: {}", dirtyPath->id,
                        root.Id, error.what());
        } catch (...) {
          dirtyPathRepo_->markFailed(dirtyPath->id,
                                     "Unknown dirty path processing error");
          spdlog::error("Dirty path {} failed for root {} with unknown error",
                        dirtyPath->id, root.Id);
        }
      }
    }

    queueService_->runTick();

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
      if (readyEvents[i].data.fd != watcher_->fd()) {
        continue;
      }

      processWatcherEvents();
    }
  }

  watcher_->stop();
  spdlog::info("Daemon stopped");
  return 0;
}
