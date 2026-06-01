#include "service/SyncService.hpp"
#include "sync/FileHasher.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string generateId() {
  static std::mt19937_64 rng(std::random_device{}());
  static std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream stream;
  stream << std::hex << dist(rng) << dist(rng);
  return stream.str();
}

std::string toMtimeString(const std::filesystem::file_time_type &time) {
  return std::to_string(time.time_since_epoch().count());
}

bool shouldMarkPendingUpload(const std::optional<EntryRecord> &existingEntry,
                             const LocalEntry &scannedEntry,
                             const std::string &scannedMtime,
                             const std::optional<std::string> &scannedHash) {
  if (!existingEntry.has_value()) {
    return true;
  }

  if (existingEntry->syncState == "deleted") {
    return true;
  }

  if (existingEntry->isDirectory != scannedEntry.isDirectory) {
    return true;
  }

  if (existingEntry->localMtime != scannedMtime) {
    return true;
  }

  const std::optional<int64_t> scannedSize =
      scannedEntry.isDirectory
          ? std::nullopt
          : std::optional<int64_t>(static_cast<int64_t>(scannedEntry.size));
  if (existingEntry->localSize != scannedSize) {
    return true;
  }

  return existingEntry->localHash != scannedHash;
}

} // namespace

SyncService::SyncService(SyncRepo &syncRepo, FileScanner &fileScanner)
    : syncRepo_(syncRepo), fileScanner_(fileScanner) {}

void SyncService::addPath() {
  const std::filesystem::path rootPath =
      std::filesystem::absolute(fileScanner_.rootPath()).lexically_normal();
  const std::string rootPathString = rootPath.string();
  auto syncRoot = syncRepo_.getSyncRootByLocalPath(rootPathString);
  if (!syncRoot.has_value()) {
    syncRoot = SyncRootRecord{
        .id = generateId(),
        .localPath = rootPathString,
        .folderId = std::nullopt,
        .enabled = true,
    };
  }

  syncRepo_.upsertSyncRoot(*syncRoot);
}

size_t SyncService::scanRoot() {
  const std::filesystem::path rootPath =
      std::filesystem::absolute(fileScanner_.rootPath()).lexically_normal();
  const std::string rootPathString = rootPath.string();
  auto syncRoot = syncRepo_.getSyncRootByLocalPath(rootPathString);
  if (!syncRoot.has_value()) {
    syncRoot = SyncRootRecord{
        .id = generateId(),
        .localPath = rootPathString,
        .folderId = std::nullopt,
        .enabled = true,
    };
    syncRepo_.upsertSyncRoot(*syncRoot);
  }

  const auto existingEntries = syncRepo_.getEntriesForSyncRoot(syncRoot->id);
  std::unordered_map<std::string, EntryRecord> existingEntriesByPath;
  existingEntriesByPath.reserve(existingEntries.size());
  for (const auto &entry : existingEntries) {
    existingEntriesByPath.emplace(entry.localPath, entry);
  }

  const auto scannedEntries = fileScanner_.scanFiles();

  std::vector<EntryRecord> entryRecords;
  entryRecords.reserve(scannedEntries.size());
  std::vector<std::string> presentPaths;
  presentPaths.reserve(scannedEntries.size());

  for (const auto &entry : scannedEntries) {
    const std::string relativePath = entry.relativePath.string();
    const std::string localMtime = toMtimeString(entry.modifiedTime);
    const auto existingEntryIt = existingEntriesByPath.find(relativePath);

    std::optional<std::string> localHash = std::nullopt;

    if (!entry.isDirectory) {
      FileHasher hasher(entry.absolutePath);
      localHash = hasher.hashFile();
    }

    const std::optional<EntryRecord> existingEntry =
        existingEntryIt != existingEntriesByPath.end()
            ? std::optional<EntryRecord>(existingEntryIt->second)
            : std::nullopt;

    presentPaths.push_back(relativePath);

    entryRecords.push_back(EntryRecord{
        .id = existingEntry.has_value() ? existingEntry->id : generateId(),
        .remoteId =
            existingEntry.has_value() ? existingEntry->remoteId : std::nullopt,
        .syncRootId = syncRoot->id,
        .remoteType = entry.isDirectory ? "folder" : "file",
        .localPath = relativePath,
        .isDirectory = entry.isDirectory,
        .parentFolderId = existingEntry.has_value()
                              ? existingEntry->parentFolderId
                              : std::nullopt,
        .encryptedName = existingEntry.has_value()
                             ? existingEntry->encryptedName
                             : std::nullopt,
        .localSize = entry.isDirectory ? std::nullopt
                                       : std::optional<int64_t>(
                                             static_cast<int64_t>(entry.size)),
        .localMtime = localMtime,
        .localHash = localHash,
        .remoteUpdatedAt = existingEntry.has_value()
                               ? existingEntry->remoteUpdatedAt
                               : std::nullopt,
        .syncState =
            shouldMarkPendingUpload(existingEntry, entry, localMtime, localHash)
                         ? "pending_upload"
                         : existingEntry->syncState,
        .lastSyncedAt = existingEntry.has_value() ? existingEntry->lastSyncedAt
                                                  : std::nullopt,
    });
  }

  syncRepo_.upsertEntries(entryRecords);
  syncRepo_.markMissingEntriesDeleted(syncRoot->id, presentPaths);
  syncRepo_.enqueuePendingUploads(syncRoot->id);
  return entryRecords.size();
}
