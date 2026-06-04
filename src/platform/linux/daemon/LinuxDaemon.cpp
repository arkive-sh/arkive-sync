#include "platform/linux/daemon/LinuxDaemon.hpp"

#include "fs/FileWatcher.hpp"
#include "service/SyncScheduler.hpp"

#include <cerrno>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <system_error>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace {

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

} // namespace

LinuxDaemon::LinuxDaemon(SyncScheduler &syncScheduler)
    : syncScheduler_(syncScheduler) {}

int LinuxDaemon::run() {
  const std::vector<WatchRoot> watchRoots = syncScheduler_.watchRoots();
  if (watchRoots.empty()) {
    throw std::runtime_error("No enabled sync paths configured.");
  }

  auto watcher = IFileWatcher::create();
  for (const auto &root : watchRoots) {
    watcher->addRoot(root);
  }
  syncScheduler_.scheduleAll();

  const ScopedFd epollFd(epoll_create1(EPOLL_CLOEXEC));
  if (epollFd.get() < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_create1 failed");
  }

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = watcher->fd();

  if (epoll_ctl(epollFd.get(), EPOLL_CTL_ADD, watcher->fd(), &event) < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "epoll_ctl add watcher failed");
  }

  while (true) {
    const int timeoutMs = syncScheduler_.nextWaitTimeoutMs();
    epoll_event readyEvents[8]{};
    const int readyCount =
        epoll_wait(epollFd.get(), readyEvents, 8, timeoutMs);

    if (readyCount < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::system_error(errno, std::generic_category(),
                              "epoll_wait failed");
    }

    for (int i = 0; i < readyCount; ++i) {
      if (readyEvents[i].data.fd != watcher->fd()) {
        continue;
      }

      for (const auto &fileEvent : watcher->poll()) {
        syncScheduler_.schedule(fileEvent);
      }
    }

    const auto scanResult = syncScheduler_.runDueScans();
    if (scanResult.scannedRoots > 0) {
      spdlog::info("Daemon scanned {} root(s), detected {} change(s)",
                   scanResult.scannedRoots, scanResult.changedEntries);
    }
  }
}
