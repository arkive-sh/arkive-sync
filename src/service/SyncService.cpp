#include "service/SyncService.hpp"
#include "sync/FileHasher.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

SyncService::SyncService(SyncRepo &syncRepo, QueueRepo &queueRepo,
                         FileScanner &fileScanner)
    : syncRepo_(syncRepo), queueRepo_(queueRepo), fileScanner_(fileScanner) {}

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
  // 1. Resolve sync root path and make sure root record exists.
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

  // 2. Load current DB view for this root.
  const auto existingEntries = syncRepo_.getEntriesForSyncRoot(syncRoot->id);
  std::unordered_map<std::string, EntryRecord> existingEntriesByPath;
  existingEntriesByPath.reserve(existingEntries.size());
  for (const auto &entry : existingEntries) {
    existingEntriesByPath.emplace(entry.localPath, entry);
  }

  // 3. Scan filesystem and process each unique relative path once.
  const auto scannedEntries = fileScanner_.scanFiles();

  std::vector<EntryRecord> entryRecords;
  entryRecords.reserve(scannedEntries.size());

  std::unordered_set<std::string> presentPaths;
  presentPaths.reserve(scannedEntries.size());

  for (const auto &entry : scannedEntries) {
    const std::string relativePath = entry.relativePath.string();
    if (!presentPaths.insert(relativePath).second) {
      continue;
    }

    const std::string localMtime = toMtimeString(entry.modifiedTime);
    const auto existingEntryIt = existingEntriesByPath.find(relativePath);

    const std::optional<EntryRecord> existingEntry =
        existingEntryIt != existingEntriesByPath.end()
            ? std::optional<EntryRecord>(existingEntryIt->second)
            : std::nullopt;

    std::optional<std::string> localHash = std::nullopt;
    if (!entry.isDirectory) {
      FileHasher hasher(entry.absolutePath);
      localHash = hasher.hashFile();
    }

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

  // 4. Persist current scan, mark missing entries deleted, queue uploads.
  syncRepo_.upsertEntries(entryRecords);
  syncRepo_.markMissingEntriesDeleted(
      syncRoot->id,
      std::vector<std::string>(presentPaths.begin(), presentPaths.end()));

  for (const auto &entry : entryRecords) {
    if (entry.syncState != "pending_upload" || entry.isDirectory) {
      continue;
    }

    queueRepo_.enqueueUpload(entry.id, entry.localPath, entry.parentFolderId,
                             entry.localSize.value_or(0));
  }

  return entryRecords.size();
}
