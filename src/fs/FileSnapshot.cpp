#include "fs/FileSnapshot.hpp"

#include <algorithm>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace {

std::string pathKey(const std::filesystem::path &path) {
  return path.lexically_normal().generic_string();
}

#if defined(_WIN32)
std::string fileIdentity(const std::filesystem::path &path) {
  HANDLE file = CreateFileW(path.c_str(), 0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE |
                                FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {};
  }

  BY_HANDLE_FILE_INFORMATION info{};
  const bool ok = GetFileInformationByHandle(file, &info);
  CloseHandle(file);
  if (!ok) {
    return {};
  }

  return std::to_string(info.dwVolumeSerialNumber) + ":" +
         std::to_string(info.nFileIndexHigh) + ":" +
         std::to_string(info.nFileIndexLow);
}
#else
std::string fileIdentity(const std::filesystem::path &path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0) {
    return {};
  }

  return std::to_string(static_cast<unsigned long long>(info.st_dev)) + ":" +
         std::to_string(static_cast<unsigned long long>(info.st_ino));
}
#endif

bool changed(const FileSnapshotEntry &before, const FileSnapshotEntry &after) {
  return before.isDirectory != after.isDirectory || before.size != after.size ||
         before.modifiedAt != after.modifiedAt;
}

} // namespace

FileSnapshot takeFileSnapshot(const std::filesystem::path &root) {
  FileSnapshot snapshot;

  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return snapshot;
  }

  const auto addEntry = [&](const std::filesystem::path &path) {
    std::error_code entryEc;
    const bool isDirectory = std::filesystem::is_directory(path, entryEc);
    if (entryEc) {
      return;
    }

    std::uintmax_t size = 0;
    if (!isDirectory) {
      size = std::filesystem::file_size(path, entryEc);
      if (entryEc) {
        size = 0;
      }
    }

    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative == ".") {
      return;
    }

    snapshot[pathKey(relative)] = FileSnapshotEntry{
        .path = path,
        .identity = fileIdentity(path),
        .size = size,
        .modifiedAt = std::filesystem::last_write_time(path, entryEc),
        .isDirectory = isDirectory,
    };
  };

  for (const auto &entry : std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::skip_permission_denied,
           ec)) {
    if (ec) {
      break;
    }
    addEntry(entry.path());
  }

  return snapshot;
}

std::vector<FileEvent> diffFileSnapshots(const std::string &rootId,
                                         const FileSnapshot &before,
                                         const FileSnapshot &after) {
  std::vector<FileEvent> events;
  std::unordered_map<std::string, const FileSnapshotEntry *> deletedByIdentity;

  for (const auto &[key, entry] : before) {
    if (!after.contains(key) && !entry.identity.empty()) {
      deletedByIdentity[entry.identity] = &entry;
    }
  }

  for (const auto &[key, entry] : after) {
    const auto old = before.find(key);
    if (old != before.end()) {
      if (changed(old->second, entry)) {
        events.push_back(FileEvent{
            .rootId = rootId,
            .path = entry.path,
            .oldPath = std::nullopt,
            .type = FileEventType::Modified,
            .isDirectory = entry.isDirectory,
        });
      }
      continue;
    }

    const auto renamedFrom =
        !entry.identity.empty() ? deletedByIdentity.find(entry.identity)
                                : deletedByIdentity.end();
    if (renamedFrom != deletedByIdentity.end()) {
      events.push_back(FileEvent{
          .rootId = rootId,
          .path = entry.path,
          .oldPath = renamedFrom->second->path,
          .type = FileEventType::Renamed,
          .isDirectory = entry.isDirectory,
      });
      deletedByIdentity.erase(renamedFrom);
      continue;
    }

    events.push_back(FileEvent{
        .rootId = rootId,
        .path = entry.path,
        .oldPath = std::nullopt,
        .type = FileEventType::Created,
        .isDirectory = entry.isDirectory,
    });
  }

  for (const auto &[key, entry] : before) {
    if (after.contains(key)) {
      continue;
    }
    if (!entry.identity.empty() && !deletedByIdentity.contains(entry.identity)) {
      continue;
    }

    events.push_back(FileEvent{
        .rootId = rootId,
        .path = entry.path,
        .oldPath = std::nullopt,
        .type = FileEventType::Deleted,
        .isDirectory = entry.isDirectory,
    });
  }

  return events;
}
