#include <catch2/catch_test_macros.hpp>

#include "support/TestFs.hpp"

#if defined(__linux__)

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#define private public
#include "platform/linux/watcher/INotifyWatcher.hpp"
#undef private

namespace {

using namespace std::chrono_literals;

std::vector<FileEvent> collectEventsFor(InotifyWatcher &watcher,
                                        std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  std::vector<FileEvent> events;

  while (std::chrono::steady_clock::now() < deadline) {
    auto batch = watcher.poll();
    events.insert(events.end(), std::make_move_iterator(batch.begin()),
                  std::make_move_iterator(batch.end()));
    std::this_thread::sleep_for(20ms);
  }

  return events;
}

bool hasEvent(const std::vector<FileEvent> &events, FileEventType type,
              const std::filesystem::path &path,
              const std::optional<std::filesystem::path> &oldPath =
                  std::nullopt) {
  return std::any_of(events.begin(), events.end(), [&](const FileEvent &event) {
    return event.type == type && event.path == path && event.oldPath == oldPath;
  });
}

std::vector<char> makeEventBuffer(int wd, uint32_t mask, uint32_t cookie,
                                  const std::string &name) {
  std::vector<char> buffer(sizeof(inotify_event) + name.size() + 1, '\0');
  auto *event = reinterpret_cast<inotify_event *>(buffer.data());
  event->wd = wd;
  event->mask = mask;
  event->cookie = cookie;
  event->len = static_cast<uint32_t>(name.size() + 1);
  std::memcpy(buffer.data() + sizeof(inotify_event), name.c_str(), name.size());
  return buffer;
}

} // namespace

TEST_CASE("InotifyWatcher emits renamed event for in-root file rename") {
  TempDir tempDir;
  const auto oldPath = tempDir.path() / "old.txt";
  const auto newPath = tempDir.path() / "new.txt";
  writeFile(oldPath, "hello");

  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = tempDir.path(),
  });

  std::filesystem::rename(oldPath, newPath);

  const auto events = collectEventsFor(watcher, 300ms);
  REQUIRE(hasEvent(events, FileEventType::Renamed, newPath, oldPath));
}

TEST_CASE("InotifyWatcher treats move into watched root as created") {
  TempDir watchedDir;
  TempDir outsideDir;
  const auto sourcePath = outsideDir.path() / "movie.txt";
  const auto targetPath = watchedDir.path() / "movie.txt";
  writeFile(sourcePath, "hello");

  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = watchedDir.path(),
  });

  std::filesystem::rename(sourcePath, targetPath);

  const auto events = collectEventsFor(watcher, 300ms);
  REQUIRE(hasEvent(events, FileEventType::Created, targetPath));
}

TEST_CASE("InotifyWatcher treats move out of watched root as deleted after expiry") {
  TempDir watchedDir;
  TempDir outsideDir;
  const auto sourcePath = watchedDir.path() / "movie.txt";
  const auto targetPath = outsideDir.path() / "movie.txt";
  writeFile(sourcePath, "hello");

  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = watchedDir.path(),
  });

  std::filesystem::rename(sourcePath, targetPath);

  const auto events = collectEventsFor(watcher, 900ms);
  REQUIRE(hasEvent(events, FileEventType::Deleted, sourcePath));
}

TEST_CASE("InotifyWatcher keeps renamed directory subtree watched") {
  TempDir tempDir;
  const auto oldDir = tempDir.path() / "folder-a";
  const auto newDir = tempDir.path() / "folder-c";
  const auto nestedFile = oldDir / "child.txt";
  std::filesystem::create_directories(oldDir);
  writeFile(nestedFile, "hello");

  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = tempDir.path(),
  });

  std::filesystem::rename(oldDir, newDir);
  const auto renameEvents = collectEventsFor(watcher, 300ms);
  REQUIRE(hasEvent(renameEvents, FileEventType::Renamed, newDir, oldDir));

  writeFile(newDir / "child.txt", "updated");
  const auto modifyEvents = collectEventsFor(watcher, 300ms);
  REQUIRE(hasEvent(modifyEvents, FileEventType::Modified, newDir / "child.txt"));
}

TEST_CASE("InotifyWatcher removes moved-out directory subtree watches after expiry") {
  TempDir watchedDir;
  TempDir outsideDir;
  const auto sourceDir = watchedDir.path() / "folder-a";
  const auto targetDir = outsideDir.path() / "folder-a";
  std::filesystem::create_directories(sourceDir);
  writeFile(sourceDir / "child.txt", "hello");

  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = watchedDir.path(),
  });

  std::filesystem::rename(sourceDir, targetDir);
  const auto moveOutEvents = collectEventsFor(watcher, 900ms);
  REQUIRE(hasEvent(moveOutEvents, FileEventType::Deleted, sourceDir));

  writeFile(targetDir / "child.txt", "updated");
  const auto outsideEvents = collectEventsFor(watcher, 300ms);
  REQUIRE_FALSE(hasEvent(outsideEvents, FileEventType::Modified,
                         targetDir / "child.txt"));
}

TEST_CASE("InotifyWatcher treats moved-from cookie zero as deleted") {
  TempDir tempDir;
  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = tempDir.path(),
  });

  REQUIRE_FALSE(watcher.watches_.empty());
  const int wd = watcher.watches_.begin()->first;
  auto eventBuffer = makeEventBuffer(wd, IN_MOVED_FROM, 0, "ghost.txt");
  auto *event = reinterpret_cast<inotify_event *>(eventBuffer.data());

  const auto fileEvent = watcher.handleEvent(*event);
  REQUIRE(fileEvent.has_value());
  REQUIRE(fileEvent->type == FileEventType::Deleted);
  REQUIRE(fileEvent->path == tempDir.path() / "ghost.txt");
  REQUIRE(fileEvent->cookie == 0);
}

TEST_CASE("InotifyWatcher treats moved-to cookie zero as created") {
  TempDir tempDir;
  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = tempDir.path(),
  });

  REQUIRE_FALSE(watcher.watches_.empty());
  const int wd = watcher.watches_.begin()->first;
  auto eventBuffer = makeEventBuffer(wd, IN_MOVED_TO, 0, "ghost.txt");
  auto *event = reinterpret_cast<inotify_event *>(eventBuffer.data());

  const auto fileEvent = watcher.handleEvent(*event);
  REQUIRE(fileEvent.has_value());
  REQUIRE(fileEvent->type == FileEventType::Created);
  REQUIRE(fileEvent->path == tempDir.path() / "ghost.txt");
  REQUIRE(fileEvent->cookie == 0);
}

TEST_CASE("InotifyWatcher suppresses move-self events and removes the watch") {
  TempDir tempDir;
  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = tempDir.path(),
  });

  REQUIRE_FALSE(watcher.watches_.empty());
  const int wd = watcher.watches_.begin()->first;
  auto eventBuffer = makeEventBuffer(wd, IN_MOVE_SELF, 0, "");
  auto *event = reinterpret_cast<inotify_event *>(eventBuffer.data());

  const auto fileEvent = watcher.handleEvent(*event);
  REQUIRE_FALSE(fileEvent.has_value());
  REQUIRE(watcher.watches_.empty());
}

TEST_CASE("InotifyWatcher suppresses delete-self events and removes the watch") {
  TempDir tempDir;
  InotifyWatcher watcher;
  watcher.addRoot(WatchRoot{
      .rootId = "root-1",
      .path = tempDir.path(),
  });

  REQUIRE_FALSE(watcher.watches_.empty());
  const int wd = watcher.watches_.begin()->first;
  auto eventBuffer = makeEventBuffer(wd, IN_DELETE_SELF, 0, "");
  auto *event = reinterpret_cast<inotify_event *>(eventBuffer.data());

  const auto fileEvent = watcher.handleEvent(*event);
  REQUIRE_FALSE(fileEvent.has_value());
  REQUIRE(watcher.watches_.empty());
}

#else

TEST_CASE("InotifyWatcher tests require Linux") {}

#endif
