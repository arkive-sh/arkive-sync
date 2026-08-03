#include <catch2/catch_test_macros.hpp>

#include "fs/FileSnapshot.hpp"
#include "support/TestFs.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <vector>

namespace {

bool hasEvent(const std::vector<FileEvent> &events, FileEventType type,
              const std::filesystem::path &path,
              const std::optional<std::filesystem::path> &oldPath =
                  std::nullopt) {
  return std::any_of(events.begin(), events.end(), [&](const FileEvent &event) {
    return event.type == type && event.path == path && event.oldPath == oldPath;
  });
}

} // namespace

TEST_CASE("FileSnapshot detects create modify delete and rename") {
  TempDir tempDir;
  const auto created = tempDir.path() / "created.txt";
  const auto renamedFrom = tempDir.path() / "old.txt";
  const auto renamedTo = tempDir.path() / "new.txt";
  const auto modified = tempDir.path() / "modified.txt";
  const auto deleted = tempDir.path() / "deleted.txt";

  writeFile(renamedFrom, "rename");
  writeFile(modified, "before");
  writeFile(deleted, "delete");

  const auto before = takeFileSnapshot(tempDir.path());

  writeFile(created, "created");
  std::filesystem::rename(renamedFrom, renamedTo);
  writeFile(modified, "after");
  std::filesystem::remove(deleted);

  const auto after = takeFileSnapshot(tempDir.path());
  const auto events = diffFileSnapshots("root-1", before, after);

  REQUIRE(hasEvent(events, FileEventType::Created, created));
  REQUIRE(hasEvent(events, FileEventType::Renamed, renamedTo, renamedFrom));
  REQUIRE(hasEvent(events, FileEventType::Modified, modified));
  REQUIRE(hasEvent(events, FileEventType::Deleted, deleted));
}
