#include "service/SyncService.hpp"
#include "fs/FileScanner.hpp"
#include "fs/FileHasher.hpp"
#include "helpers/PathCodec.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr size_t kScanUpsertBatchSize = 500;

std::string generateId() {
  static std::mt19937_64 rng(std::random_device{}());
  static std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream stream;
  stream << std::hex << dist(rng) << dist(rng);
  return stream.str();
}

std::string toMtimeString(const std::filesystem::file_time_type &time) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time.time_since_epoch())
                      .count();
  return std::to_string(static_cast<long long>(ms));
}

bool shouldMarkPendingUpload(const std::optional<EntryRecord> &existingEntry,
                             const LocalEntry &scannedEntry) {
  if (!existingEntry.has_value()) {
    return true;
  }

  if (existingEntry->syncState == "deleted") {
    return true;
  }

  if (existingEntry->isDirectory != scannedEntry.isDirectory) {
    return true;
  }
  return false;
}

std::optional<EntryRecord>
toEntryRecord(const std::optional<EntryIdentity> &existingEntry,
              const std::string &syncRootId, const std::string &localPath) {
  if (!existingEntry.has_value()) {
    return std::nullopt;
  }

  return EntryRecord{
      .id = existingEntry->id,
      .remoteId = existingEntry->remoteId,
      .syncRootId = syncRootId,
      .remoteType = existingEntry->isDirectory ? "folder" : "file",
      .localPath = localPath,
      .isDirectory = existingEntry->isDirectory,
      .parentFolderId = existingEntry->parentFolderId,
      .encryptedName = existingEntry->encryptedName,
      .localSize = existingEntry->localSize,
      .localMtime = existingEntry->localMtime,
      .localHash = existingEntry->localHash,
      .remoteUpdatedAt = existingEntry->remoteUpdatedAt,
      .syncState = existingEntry->syncState,
      .lastSyncedAt = existingEntry->lastSyncedAt,
  };
}

bool isUnchanged(const EntryIdentity &existingEntry, bool isDirectory,
                 const std::optional<int64_t> &localSize,
                 const std::string &localMtime) {
  if (existingEntry.syncState == "deleted") {
    return false;
  }
  if (existingEntry.isDirectory != isDirectory) {
    return false;
  }
  if (existingEntry.localSize != localSize) {
    return false;
  }
  return existingEntry.localMtime == localMtime;
}

} // namespace

SyncService::SyncService(SyncRepo &syncRepo, QueueRepo &queueRepo,
                         RustCrypto &crypto)
    : syncRepo_(syncRepo), queueRepo_(queueRepo), crypto_(crypto) {}

void SyncService::addPath(const std::filesystem::path &rootPathInput) {
  FileScanner fileScanner(rootPathInput);
  const std::filesystem::path rootPath =
      std::filesystem::absolute(fileScanner.rootPath()).lexically_normal();
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

size_t SyncService::scanRoot(const std::filesystem::path &rootPathInput) {
  FileScanner fileScanner(rootPathInput);
  const std::filesystem::path rootPath =
      std::filesystem::absolute(fileScanner.rootPath()).lexically_normal();
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

  auto scanSession = syncRepo_.createScanSession();
  std::vector<EntryUpsertRecord> entryRecords;
  entryRecords.reserve(kScanUpsertBatchSize);
  size_t changedCount = 0;

  fileScanner.scanFiles([&](const LocalEntry &entry) {
    const std::string relativePath = PathCodec::toDbRelative(entry.relativePath);
    const std::string localPathHash = syncRepo_.hashLocalPath(relativePath);
    scanSession.markPathSeen(localPathHash);
    const std::string localMtime = toMtimeString(entry.modifiedTime);
    const std::optional<EntryIdentity> existingScanState =
        scanSession.getEntryIdentityByLocalPathHash(syncRoot->id, localPathHash);
    const std::optional<EntryRecord> existingEntry =
        toEntryRecord(existingScanState, syncRoot->id, relativePath);

    const std::optional<int64_t> localSize =
        entry.isDirectory
            ? std::nullopt
            : std::optional<int64_t>(static_cast<int64_t>(entry.size));

    if (existingScanState.has_value() &&
        isUnchanged(*existingScanState, entry.isDirectory, localSize,
                    localMtime)) {
      return;
    }

    std::optional<std::string> localHash =
        existingEntry.has_value() ? existingEntry->localHash : std::nullopt;
    std::string syncState =
        existingEntry.has_value() ? existingEntry->syncState : "pending_upload";

    const bool needsHash =
        !entry.isDirectory &&
        (!existingEntry.has_value() || existingEntry->localSize != localSize ||
         existingEntry->localMtime != localMtime ||
         existingEntry->localHash == std::nullopt ||
         existingEntry->syncState == "deleted");

    if (needsHash) {
      FileHasher hasher(entry.absolutePath, crypto_);
      localHash = hasher.hashFile();

      if (!existingEntry.has_value() || existingEntry->syncState == "deleted" ||
          existingEntry->localHash != localHash) {
        syncState = "pending_upload";
      }
    } else if (shouldMarkPendingUpload(existingEntry, entry)) {
      syncState = "pending_upload";
    }

    entryRecords.push_back(EntryUpsertRecord{
        .entry =
            EntryRecord{
                .id = existingEntry.has_value() ? existingEntry->id : generateId(),
                .remoteId = existingEntry.has_value() ? existingEntry->remoteId
                                                      : std::nullopt,
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
                .localSize = localSize,
                .localMtime = localMtime,
                .localHash = localHash,
                .remoteUpdatedAt = existingEntry.has_value()
                                       ? existingEntry->remoteUpdatedAt
                                       : std::nullopt,
                .syncState = syncState,
                .lastSyncedAt = existingEntry.has_value()
                                    ? existingEntry->lastSyncedAt
                                    : std::nullopt,
            },
        .localPathHash = localPathHash,
    });

    if (entryRecords.size() >= kScanUpsertBatchSize) {
      changedCount += syncRepo_.upsertEntries(entryRecords);
      entryRecords.clear();
    }
  });

  if (!entryRecords.empty()) {
    changedCount += syncRepo_.upsertEntries(entryRecords);
  }
  changedCount += syncRepo_.markMissingEntriesDeleted(syncRoot->id);
  syncRepo_.releaseMemory();

  return changedCount;
}
