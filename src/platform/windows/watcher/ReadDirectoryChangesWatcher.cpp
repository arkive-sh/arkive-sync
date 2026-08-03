#include "platform/windows/watcher/ReadDirectoryChangesWatcher.hpp"

#include <array>
#include <stdexcept>
#include <system_error>

namespace {

std::system_error winError(const char *message) {
  return std::system_error(static_cast<int>(GetLastError()),
                           std::system_category(), message);
}

void watchLoop(ReadDirectoryChangesWatcher::RootState *root) {
  std::array<char, 64 * 1024> buffer{};
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (event == nullptr) {
    root->dirty = true;
    return;
  }

  while (root->running.load()) {
    ResetEvent(event);
    DWORD bytesReturned = 0;
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;

    const BOOL ok = ReadDirectoryChangesW(
        root->dir, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_CREATION,
        nullptr, &overlapped, nullptr);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
      if (!root->running.load()) {
        break;
      }
      root->dirty = true;
      break;
    }

    while (root->running.load()) {
      const DWORD wait = WaitForSingleObject(event, 250);
      if (wait == WAIT_OBJECT_0) {
        break;
      }
      if (wait != WAIT_TIMEOUT) {
        root->dirty = true;
        CloseHandle(event);
        return;
      }
    }

    if (!root->running.load()) {
      CancelIoEx(root->dir, &overlapped);
      break;
    }

    if (!GetOverlappedResult(root->dir, &overlapped, &bytesReturned, FALSE)) {
      root->dirty = true;
      break;
    }

    if (bytesReturned == 0) {
      root->dirty = true;
      continue;
    }
    root->dirty = true;
  }

  CloseHandle(event);
}

} // namespace

ReadDirectoryChangesWatcher::~ReadDirectoryChangesWatcher() { stop(); }

void ReadDirectoryChangesWatcher::addRoot(const WatchRoot &root) {
  auto state = std::make_unique<RootState>();
  state->rootId = root.rootId;
  state->path = root.path;
  state->snapshot = takeFileSnapshot(root.path);
  state->dir = CreateFileW(root.path.c_str(), FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE |
                               FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                           nullptr);
  if (state->dir == INVALID_HANDLE_VALUE) {
    throw winError("CreateFileW watch root failed");
  }

  state->thread = std::thread(watchLoop, state.get());

  std::lock_guard lock(mutex_);
  roots_.push_back(std::move(state));
}

std::vector<FileEvent> ReadDirectoryChangesWatcher::poll() {
  std::vector<FileEvent> events;
  std::lock_guard lock(mutex_);

  for (auto &root : roots_) {
    if (!root->dirty.exchange(false)) {
      continue;
    }

    auto next = takeFileSnapshot(root->path);
    auto diff = diffFileSnapshots(root->rootId, root->snapshot, next);
    root->snapshot = std::move(next);
    events.insert(events.end(), std::make_move_iterator(diff.begin()),
                  std::make_move_iterator(diff.end()));
  }

  return events;
}

void ReadDirectoryChangesWatcher::stop() {
  std::vector<std::unique_ptr<RootState>> roots;
  {
    std::lock_guard lock(mutex_);
    if (stopped_) {
      return;
    }
    stopped_ = true;
    roots.swap(roots_);
  }

  for (auto &root : roots) {
    root->running = false;
    if (root->dir != INVALID_HANDLE_VALUE) {
      CancelIoEx(root->dir, nullptr);
      CloseHandle(root->dir);
      root->dir = INVALID_HANDLE_VALUE;
    }
    if (root->thread.joinable()) {
      root->thread.join();
    }
  }
}
