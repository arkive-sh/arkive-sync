#pragma once
#include "fs/FileWatcher.hpp"
#include "./INotifyFD.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <spdlog/spdlog.h>
#include <system_error>
#include <unordered_map>
#include <vector>

struct WatchInfo {
  int wd;
  std::string rootId;
  std::filesystem::path path;
  bool recursive;
};

class InotifyWatcher : public IFileWatcher {
public:
  InotifyWatcher() = default;
  ~InotifyWatcher() override { stop(); }

  InotifyWatcher(const InotifyWatcher &) = delete;
  InotifyWatcher &operator=(const InotifyWatcher &) = delete;

  void addRoot(const WatchRoot &root) override { addWatchRecursive(root); }

  std::vector<FileEvent> poll() override {
    if (stopped_) {
      return {};
    }

    std::vector<FileEvent> events;
    std::array<char, 64 * 1024> buffer{};

    while (true) {
      ssize_t len = read(fd_.get(), buffer.data(), buffer.size());

      if (len < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        throw std::system_error(errno, std::generic_category(),
                                "inotify read failed");
      }

      if (len == 0) {
        break;
      }

      size_t offset = 0;
      while (offset < static_cast<size_t>(len)) {
        auto *event =
            reinterpret_cast<inotify_event *>(buffer.data() + offset);
        if (auto fileEvent = handleEvent(*event)) {
          events.push_back(std::move(*fileEvent));
        }
        offset += sizeof(inotify_event) + event->len;
      }
    }

    return events;
  }

  int fd() const override { return fd_.get(); }

  void stop() override {
    if (stopped_) {
      return;
    }

    stopped_ = true;

    for (const auto &[wd, _] : watches_) {
      inotify_rm_watch(fd_.get(), wd);
    }

    watches_.clear();
  }

private:
  static constexpr uint32_t Mask =
      IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO |
      IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB | IN_Q_OVERFLOW;

  void addWatch(const WatchRoot &root, const std::filesystem::path &path) {
    int wd = inotify_add_watch(fd_.get(), path.c_str(), Mask);

    if (wd < 0) {
      spdlog::warn("Failed to watch {}: {}", path.string(),
                   std::strerror(errno));
      return;
    }

    watches_[wd] = WatchInfo{
        .wd = wd,
        .rootId = root.rootId,
        .path = path,
        .recursive = true,
    };
  }

  void addWatchRecursive(const WatchRoot &root) {
    addWatch(root, root.path);

    std::error_code ec;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(
             root.path,
             std::filesystem::directory_options::skip_permission_denied, ec)) {
      if (ec) {
        spdlog::warn("Failed to walk {} for watches: {}", root.path.string(),
                     ec.message());
        break;
      }

      if (entry.is_directory(ec) && !ec) {
        addWatch(root, entry.path());
      }
    }
  }

  static FileEventType toEventType(uint32_t mask) {
    if (mask & IN_Q_OVERFLOW) {
      return FileEventType::Overflow;
    }
    if (mask & (IN_DELETE | IN_DELETE_SELF)) {
      return FileEventType::Deleted;
    }
    if (mask & (IN_MOVED_FROM | IN_MOVED_TO | IN_MOVE_SELF)) {
      return FileEventType::Moved;
    }
    if (mask & (IN_CLOSE_WRITE | IN_MODIFY)) {
      return FileEventType::Modified;
    }
    if (mask & IN_CREATE) {
      return FileEventType::Created;
    }
    if (mask & IN_ATTRIB) {
      return FileEventType::AttributeChanged;
    }
    return FileEventType::Unknown;
  }

  std::optional<FileEvent> handleEvent(const inotify_event &event) {
    if (event.mask & IN_Q_OVERFLOW) {
      return FileEvent{
          .rootId = "",
          .path = {},
          .type = FileEventType::Overflow,
          .isDirectory = false,
      };
    }

    auto it = watches_.find(event.wd);
    if (it == watches_.end()) {
      return std::nullopt;
    }

    const WatchInfo watch = it->second;
    std::filesystem::path fullPath = watch.path;

    if (event.len > 0 && event.name[0] != '\0') {
      fullPath /= event.name;
    }

    const bool isDirectory = static_cast<bool>(event.mask & IN_ISDIR);

    if (isDirectory && (event.mask & (IN_CREATE | IN_MOVED_TO))) {
      addWatchRecursive(WatchRoot{
          .rootId = watch.rootId,
          .path = fullPath,
      });
    }

    if (event.mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
      inotify_rm_watch(fd_.get(), event.wd);
      watches_.erase(event.wd);
    }

    return FileEvent{
        .rootId = watch.rootId,
        .path = fullPath,
        .type = toEventType(event.mask),
        .isDirectory = isDirectory,
    };
  }

private:
  InotifyFd fd_;
  bool stopped_{false};
  std::unordered_map<int, WatchInfo> watches_;
};
