#include "platform/linux/daemon/LinuxDaemon.hpp"

#include "fs/FileWatcher.hpp"

#include <cerrno>
#include <csignal>
#include <spdlog/spdlog.h>
#include <stdexcept>
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

LinuxDaemon::LinuxDaemon(std::unique_ptr<IFileWatcher> watcher)
    : watcher_(std::move(watcher)) {}

LinuxDaemon::~LinuxDaemon() = default;

int LinuxDaemon::run() {
  gStopRequested = 0;
  ScopedSignalHandlers signalHandlers;
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

  spdlog::warn("Daemon orchestration has been removed; running passive watcher loop");

  while (!gStopRequested) {
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
      }
    }
  }

  watcher_->stop();
  spdlog::info("Daemon stopped");
  return 0;
}
