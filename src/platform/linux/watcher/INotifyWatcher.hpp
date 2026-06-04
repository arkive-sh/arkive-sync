#pragma once
#include "fs/FileWatcher.hpp"
#include "./INotifyFD.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iterator>
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

struct PendingMove {
  std::string rootId;
  std::filesystem::path oldPath;
  bool isDirectory{false};
  std::chrono::steady_clock::time_point seenAt;
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

    auto expiredMoves = drainExpiredMoves();
    events.insert(events.end(), std::make_move_iterator(expiredMoves.begin()),
                  std::make_move_iterator(expiredMoves.end()));

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
    pathToWd_.clear();
    pendingMoves_.clear();
  }

private:
  static constexpr uint32_t Mask =
      IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO |
      IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ATTRIB | IN_Q_OVERFLOW;

  static std::string normalizePathKey(const std::filesystem::path &path) {
    return path.lexically_normal().string();
  }

  static bool isSameOrChildPath(const std::filesystem::path &parent,
                                const std::filesystem::path &child) {
    const auto normalizedParent = parent.lexically_normal();
    const auto normalizedChild = child.lexically_normal();

    auto parentIt = normalizedParent.begin();
    auto childIt = normalizedChild.begin();

    for (; parentIt != normalizedParent.end(); ++parentIt, ++childIt) {
      if (childIt == normalizedChild.end() || *parentIt != *childIt) {
        return false;
      }
    }

    return true;
  }

  void removeWatch(int wd) {
    auto it = watches_.find(wd);
    if (it == watches_.end()) {
      return;
    }

    pathToWd_.erase(normalizePathKey(it->second.path));
    inotify_rm_watch(fd_.get(), wd);
    watches_.erase(it);
  }

  void removeWatchesUnder(const std::filesystem::path &path) {
    std::vector<int> toRemove;

    for (const auto &[wd, info] : watches_) {
      if (isSameOrChildPath(path, info.path)) {
        toRemove.push_back(wd);
      }
    }

    for (int wd : toRemove) {
      removeWatch(wd);
    }
  }

  void addWatch(const WatchRoot &root, const std::filesystem::path &path) {
    const std::filesystem::path normalizedPath = path.lexically_normal();
    const std::string pathKey = normalizePathKey(normalizedPath);
    if (pathToWd_.contains(pathKey)) {
      return;
    }

    int wd = inotify_add_watch(fd_.get(), normalizedPath.c_str(), Mask);

    if (wd < 0) {
      spdlog::warn("Failed to watch {}: {}", normalizedPath.string(),
                   std::strerror(errno));
      return;
    }

    auto existing = watches_.find(wd);
    if (existing != watches_.end()) {
      pathToWd_.erase(normalizePathKey(existing->second.path));
    }

    watches_[wd] = WatchInfo{
        .wd = wd,
        .rootId = root.rootId,
        .path = normalizedPath,
        .recursive = true,
    };
    pathToWd_[pathKey] = wd;
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
    if (mask & IN_MOVED_FROM) {
      return FileEventType::MovedFrom;
    }
    if (mask & IN_MOVED_TO) {
      return FileEventType::MovedTo;
    }
    if (mask & (IN_DELETE | IN_DELETE_SELF)) {
      return FileEventType::Deleted;
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

  std::vector<FileEvent>
  drainExpiredMoves(std::chrono::milliseconds maxAge =
                        std::chrono::milliseconds(500)) {
    std::vector<FileEvent> events;
    const auto now = std::chrono::steady_clock::now();

    for (auto it = pendingMoves_.begin(); it != pendingMoves_.end();) {
      if (now - it->second.seenAt < maxAge) {
        ++it;
        continue;
      }

      events.push_back(FileEvent{
          .rootId = it->second.rootId,
          .path = it->second.oldPath,
          .oldPath = std::nullopt,
          .type = FileEventType::Deleted,
          .isDirectory = it->second.isDirectory,
          .cookie = it->first,
      });

      if (it->second.isDirectory) {
        removeWatchesUnder(it->second.oldPath);
      }

      it = pendingMoves_.erase(it);
    }

    return events;
  }

  std::optional<FileEvent> handleEvent(const inotify_event &event) {
    if (event.mask & IN_Q_OVERFLOW) {
      return FileEvent{
          .rootId = "",
          .path = {},
          .oldPath = std::nullopt,
          .type = FileEventType::Overflow,
          .isDirectory = false,
          .cookie = event.cookie,
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

    if ((event.mask & IN_MOVED_FROM) && event.cookie != 0) {
      pendingMoves_[event.cookie] = PendingMove{
          .rootId = watch.rootId,
          .oldPath = fullPath,
          .isDirectory = isDirectory,
          .seenAt = std::chrono::steady_clock::now(),
      };
      return std::nullopt;
    }

    if ((event.mask & IN_MOVED_FROM) && event.cookie == 0) {
      return FileEvent{
          .rootId = watch.rootId,
          .path = fullPath,
          .oldPath = std::nullopt,
          .type = FileEventType::Deleted,
          .isDirectory = isDirectory,
          .cookie = 0,
      };
    }

    if ((event.mask & IN_MOVED_TO) && event.cookie != 0) {
      auto pending = pendingMoves_.find(event.cookie);

      if (pending != pendingMoves_.end() &&
          pending->second.rootId == watch.rootId) {
        const std::filesystem::path oldPath = pending->second.oldPath;
        const bool wasDirectory = pending->second.isDirectory;
        pendingMoves_.erase(pending);

        if (isDirectory || wasDirectory) {
          removeWatchesUnder(oldPath);
          addWatchRecursive(WatchRoot{
              .rootId = watch.rootId,
              .path = fullPath,
          });
        }

        return FileEvent{
            .rootId = watch.rootId,
            .path = fullPath,
            .oldPath = oldPath,
            .type = FileEventType::Renamed,
            .isDirectory = isDirectory || wasDirectory,
            .cookie = event.cookie,
        };
      }

      if (isDirectory) {
        addWatchRecursive(WatchRoot{
            .rootId = watch.rootId,
            .path = fullPath,
        });
      }

      return FileEvent{
          .rootId = watch.rootId,
          .path = fullPath,
          .oldPath = std::nullopt,
          .type = FileEventType::Created,
          .isDirectory = isDirectory,
          .cookie = event.cookie,
      };
    }

    if ((event.mask & IN_MOVED_TO) && event.cookie == 0) {
      if (isDirectory) {
        addWatchRecursive(WatchRoot{
            .rootId = watch.rootId,
            .path = fullPath,
        });
      }

      return FileEvent{
          .rootId = watch.rootId,
          .path = fullPath,
          .oldPath = std::nullopt,
          .type = FileEventType::Created,
          .isDirectory = isDirectory,
          .cookie = 0,
      };
    }

    if (isDirectory && (event.mask & (IN_CREATE | IN_MOVED_TO))) {
      addWatchRecursive(WatchRoot{
          .rootId = watch.rootId,
          .path = fullPath,
      });
    }

    if (event.mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
      removeWatch(event.wd);
    }

    return FileEvent{
        .rootId = watch.rootId,
        .path = fullPath,
        .oldPath = std::nullopt,
        .type = toEventType(event.mask),
        .isDirectory = isDirectory,
        .cookie = event.cookie,
    };
  }

private:
  InotifyFd fd_;
  bool stopped_{false};
  std::unordered_map<int, WatchInfo> watches_;
  std::unordered_map<std::string, int> pathToWd_;
  std::unordered_map<uint32_t, PendingMove> pendingMoves_;
};
