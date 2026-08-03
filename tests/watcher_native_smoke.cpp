#include "fs/FileWatcher.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

void writeFile(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << contents;
}

std::vector<FileEvent> collectEventsFor(IFileWatcher &watcher,
                                        std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  std::vector<FileEvent> events;

  while (std::chrono::steady_clock::now() < deadline) {
    auto batch = watcher.poll();
    events.insert(events.end(), std::make_move_iterator(batch.begin()),
                  std::make_move_iterator(batch.end()));
    std::this_thread::sleep_for(50ms);
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

int fail(const char *message) {
  std::cerr << "watcher smoke failed: " << message << "\n";
  return 1;
}

} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("arkive-watcher-smoke-" + std::to_string(std::rand()));
  std::filesystem::create_directories(root);

  try {
    auto watcher = IFileWatcher::create();
    watcher->addRoot(WatchRoot{
        .rootId = "root-1",
        .path = root,
    });
    std::this_thread::sleep_for(300ms);

    const auto created = root / "created.txt";
    writeFile(created, "created");
    auto events = collectEventsFor(*watcher, 4s);
    if (!hasEvent(events, FileEventType::Created, created)) {
      return fail("missing create event");
    }

    writeFile(created, "modified");
    events = collectEventsFor(*watcher, 4s);
    if (!hasEvent(events, FileEventType::Modified, created)) {
      return fail("missing modify event");
    }

    const auto renamed = root / "renamed.txt";
    std::filesystem::rename(created, renamed);
    events = collectEventsFor(*watcher, 4s);
    if (!hasEvent(events, FileEventType::Renamed, renamed, created)) {
      return fail("missing rename event");
    }

    const auto nested = root / "folder" / "child.txt";
    std::filesystem::create_directories(nested.parent_path());
    writeFile(nested, "nested");
    events = collectEventsFor(*watcher, 4s);
    if (!hasEvent(events, FileEventType::Created, nested)) {
      return fail("missing recursive create event");
    }

    std::filesystem::remove(renamed);
    events = collectEventsFor(*watcher, 4s);
    if (!hasEvent(events, FileEventType::Deleted, renamed)) {
      return fail("missing delete event");
    }

    watcher->stop();
    std::filesystem::remove_all(root);
  } catch (const std::exception &error) {
    std::cerr << "watcher smoke failed: " << error.what() << "\n";
    std::filesystem::remove_all(root);
    return 1;
  }

  return 0;
}
