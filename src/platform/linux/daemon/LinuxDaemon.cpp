#include "platform/linux/daemon/LinuxDaemon.hpp"

#include "crypto/RustCrypto.hpp"
#include "db/Sqlite.hpp"
#include "fs/FileWatcher.hpp"
#include "repo/DirtyPathRepo.hpp"
#include "repo/EntryRepo.hpp"
#include "repo/ScanRepo.hpp"
#include "repo/SyncRepo.hpp"
#include "service/SyncService.hpp"
#include "sync/RootScanner.hpp"

#include <cerrno>
#include <csignal>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <system_error>
#include <unistd.h>

namespace {

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
                         std::unique_ptr<SyncService> syncService,
                         std::unique_ptr<RootScanner> rootScanner,
                         std::unique_ptr<IFileWatcher> watcher)
    : db_(std::move(db)), crypto_(std::move(crypto)),
      syncRepo_(std::move(syncRepo)), scanRepo_(std::move(scanRepo)),
      dirtyPathRepo_(std::move(dirtyPathRepo)),
      entryRepo_(std::move(entryRepo)), syncService_(std::move(syncService)),
      rootScanner_(std::move(rootScanner)), watcher_(std::move(watcher)) {}

LinuxDaemon::~LinuxDaemon() = default;

int LinuxDaemon::run() {
  gStopRequested = 0;
  ScopedSignalHandlers signalHandlers;
  const auto roots = syncService_->getSyncRoots();

  for (const auto &root : roots) {
    if (!root.enabled) {
      continue;
    }

    watcher_->addRoot(WatchRoot{
        .rootId = root.Id,
        .path = root.localPath,
    });

    if (!rootScanner_->scanRoot(root.Id)) {
      spdlog::error("Failed to start scan for sync root {}", root.Id);
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

  while (!gStopRequested) {
    for (const auto &root : roots) {
      if (!root.enabled) {
        continue;
      }

      if (scanRepo_->hasRunningScanJob(root.Id) &&
          !rootScanner_->scanRoot(root.Id)) {
        spdlog::error("Failed to continue scan for sync root {}", root.Id);
      }
    }

    epoll_event readyEvents[8]{};
    const int readyCount = epoll_wait(epollFd.get(), readyEvents, 8, 1000);

    if (readyCount < 0) {
      if (errno == EINTR) {
        continue;
      }

      throw std::system_error(errno, std::generic_category(),
                              "epoll_wait failed");
    }

    for (int i = 0; i < readyCount; ++i) {
      if (readyEvents[i].data.fd != watcher_->fd()) {
        continue;
      }

      for (const auto &fileEvent : watcher_->poll()) {
        if (fileEvent.oldPath.has_value()) {
          spdlog::info("watch event type={} path={} old_path={}",
                       eventTypeName(fileEvent.type), fileEvent.path.string(),
                       fileEvent.oldPath->string());
        } else {
          spdlog::info("watch event type={} path={}",
                       eventTypeName(fileEvent.type), fileEvent.path.string());
        }

        dirtyPathRepo_->record(fileEvent);
      }
    }
  }

  watcher_->stop();
  spdlog::info("Daemon stopped");
  return 0;
}
