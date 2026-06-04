#pragma once
#include <stdexcept>
#include <sys/inotify.h>
#include <unistd.h>

class InotifyFd {
public:
  InotifyFd() {
    fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) {
      throw std::runtime_error("inotify_init1 failed");
    }
  }

  ~InotifyFd() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  InotifyFd(const InotifyFd &) = delete;
  InotifyFd &operator=(const InotifyFd &) = delete;

  InotifyFd(InotifyFd &&other) noexcept {
    fd_ = other.fd_;
    other.fd_ = -1;
  }

  InotifyFd &operator=(InotifyFd &&other) noexcept {
    if (this != &other) {
      if (fd_ >= 0)
        close(fd_);
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  int get() const { return fd_; }

private:
  int fd_{-1};
};
