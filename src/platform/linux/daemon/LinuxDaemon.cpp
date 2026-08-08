#include "platform/linux/daemon/LinuxDaemon.hpp"

#include "fs/FileWatcher.hpp"
#include "platform/daemon/PollingDaemon.hpp"

#include <cerrno>
#include <sys/epoll.h>
#include <system_error>
#include <unistd.h>
#include <utility>

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

LinuxDaemon::LinuxDaemon(DaemonServices services)
    : services_(std::move(services)) {}

LinuxDaemon::~LinuxDaemon() = default;

int LinuxDaemon::run() {
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

  PollingDaemon daemon(
      std::move(services_), [epollFd = epollFd.get()] {
        epoll_event readyEvents[8]{};
        const int readyCount = epoll_wait(epollFd, readyEvents, 8, 1000);
        if (readyCount < 0 && errno != EINTR) {
          throw std::system_error(errno, std::generic_category(),
                                  "epoll_wait failed");
        }
      });
  return daemon.run();
}
